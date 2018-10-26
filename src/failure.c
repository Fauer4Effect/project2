#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "failure.h"
#include "messages.h"
#include "serialize.h"
#include "logging.h"

#define FAILURE_PORT "66666"

// bind the failure dector to a port
int bind_failure_detector()
{
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int sockfd;
    int yes = 1;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;        // ipv4 or ipv6
    hints.ai_socktype = SOCK_DGRAM;     // UDP
    hints.ai_flags = AI_PASSIVE;        

    if ((rv = getaddrinfo(NULL, FAILURE_PORT, &hints, &servinfo)) != 0)
    {
        logger(0, LOG_LEVEL, PROCESS_ID, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }

    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd < 0)
        {
            continue;
        }
        
        // prevent "address already in use"
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            continue;
        }
        break;
    }

    if (p == NULL)
    {
        perror("listener failed to bind socket");
        exit(1);
    }
    freeaddrinfo(servinfo);
    logger(0, LOG_LEVEL, PROCESS_ID, "Bound failure detector socket\n");

    return sockfd;
}

// multicast the heartbeat
void send_heartbeat(int process_id)
{
    HeartBeat *beat = malloc(sizeof(HeartBeat));
    beat->process_id = PROCESS_ID;

    unsigned char *buf = malloc(sizeof(HeartBeat));
    pack_heart_beat(beat, buf);

    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;

    int i;
    for (i = 0; i < NUM_HOSTS; i++)
    {   
        if (MEMBERSHIP_LIST[i] == 0)
        {
            continue;
        }

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        
        if ((rv = getaddrinfo(HOSTS[i], FAILURE_PORT, &hints, &servinfo)) != 0)
        {
            logger(1, LOG_LEVEL, PROCESS_ID, "getaddrinfo %s\n", gai_strerror(rv));
            exit(1);
        }

        for (p = servinfo; p != NULL; p = p->ai_next)
        {
            if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
                continue;
            break;
        }

        if (p == NULL)
        {
            logger(1, LOG_LEVEL, PROCESS_ID, "Failed to connect to peer\n");
            exit(1);
        }
        freeaddrinfo(servinfo);
        logger(0, LOG_LEVEL, PROCESS_ID, "Connected to peer\n");

        sendto(sockfd, buf, sizeof(HeartBeat), 0, p->ai_addr, p->ai_addrlen);

        close(sockfd);
    }
    logger(0, LOG_LEVEL, PROCESS_ID, "HeartBeat Sent\n");

    return;
}

void get_heartbeat(int sockfd)
{
    struct sockaddr_storage their_addr;
    socklen_t addr_len = sizeof(their_addr);
    int numbytes;
    unsigned char buf[sizeof(HeartBeat)];
    memset(&buf, 0, sizeof(buf));

    if ((numbytes = recvfrom(sockfd, buf, sizeof(HeartBeat), 0, 
        (struct sockaddr *)&their_addr, &addr_len)) != sizeof(HeartBeat))
    {
        logger(1, LOG_LEVEL, PROCESS_ID, "Did not receive full HeartBeat\n");
        return;
    }

    HeartBeat *beat = malloc(sizeof(HeartBeat));
    unpack_heart_beat(beat, buf);

    struct timeval cur_time;
    gettimeofday(&cur_time, NULL);
    RECEIVED_HEARTBEATS[(beat->process_id)-1]->recvd_time = malloc(sizeof(struct timeval));
    RECEIVED_HEARTBEATS[(beat->process_id)-1]->recvd_time->tv_sec = cur_time.tv_sec;
    RECEIVED_HEARTBEATS[(beat->process_id)-1]->recvd_time->tv_usec = cur_time.tv_usec;

    return;
}
