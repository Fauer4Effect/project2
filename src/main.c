#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>

#include "messages.h"
#include "serialize.h"
#include "logging.h"

#define LOG_LEVEL DEBUG                             // set log level to debug for logging
#define max(A, B) ((A) > (B) ? (A) : (B))

int PORT;
int PROCESS_ID;
char **HOSTS;
int NUM_HOSTS;
int *MEMBERSHIP_LIST;      // void pointer because we will malloc the array of ints later

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

char **open_parse_hostfile(char *hostfile)
{
    char hostname[64];
    int cur_hostname = gethostname(hostname, 63);
    log(0, LOG_LEVEL, "Got hostname: %s\n", cur_hostname);
    if (cur_hostname == -1)
    {
        log(1, LOG_LEVEL, "Could not get hostname\n");
        exit(1);
    }

    FILE *fp = fopen(hostfile, "r");
    if (fp == NULL)
    {
        log(1, LOG_LEVEL, "Could not open hostfile\n");
        exit(1);
    }
    log(0, LOG_LEVEL, "Opened hostfile\n");

    // get number of lines in hostfile
    int numLines = 0;
    char chr;
    chr = getc(fp);
    while (chr != EOF) 
    {
        if (chr == '\n')
            numLines ++;
        chr = getc(fp);
    }
    log(0, LOG_LEVEL, "Hostfile is %d lines\n", numLines);
    fseek(fp, 0, SEEK_SET);     // reset to beginning

    // malloc 2-D array of hostnames
    char **hosts = malloc(numLines * sizeof(char *));
    // keep track of size of host file
    NUM_HOSTS = numLines;
    if (hosts == NULL)
    {
        log(1, LOG_LEVEL, "Could not malloc hosts array\n");
        exit(1);
    }

    // read in all the host names
    int i = 0;
    for(i = 0; i < NUM_HOSTS; i++)
    {
        hosts[i] = (char *)malloc(255);
        if (hosts[i] == NULL)
        {
            log(1, LOG_LEVEL, "Error on malloc");
            exit(1);
        }
        fgets(hosts[i], 255, fp);
        char *newline_pos;
        if ((newline_pos=strchr(hosts[i], '\n')) != NULL)
            *newline_pos = '\0';
        log(0, LOG_LEVEL, "Host %s read in\n", hosts[i]);
        if ((strcmp(hosts[i], hostname)) == 0)
        {
            PROCESS_ID = i;
            log(0, LOG_LEVEL, "Process id: %d\n", PROCESS_ID);
        }
    }

    return hosts;
}

void add_to_membership_list(int process_id)
{
    int i;
    int curID;
    for (i = 0; i < NUM_HOSTS; i++)
    {
        curID = MEMBERSHIP_LIST[i];
        if (curID == 0)
        {
            MEMBERSHIP_LIST[i] = process_id;
            log(0, LOG_LEVEL, "Stored process %d in membership list\n", process_id);
            return;
        }
    }
    log(1, LOG_LEVEL, "Membership list full, addition failed\n");
    exit(1);
}

void request_to_join()
{
    Header *header = malloc(sizeof(Header));
    header->msg_type = 4;                       // join message
    header->size = sizeof(JoinMessage);

    JoinMessage *join = malloc(sizeof(JoinMessage));
    join->process_id = PROCESS_ID;

    unsigned char *buf = malloc(sizeof(Header) + sizeof(JoinMessage));

    pack_header(header, buf);
    pack_join_message(join, buf+8);             // +8 because need to offset for header
    log(0, LOG_LEVEL, "Message and header setup and packed\n");

    int sockfd, numbytes;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(HOSTS[0], PORT, &hints, &servinfo)) != 0)
    {
        log(1, LOG_LEVEL, "getaddrinfo %s\n", gai_strerror(rv));
        exit(1);
    }

    for(p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            continue;
        }
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            continue;
        }
        break;
    }

    if (p == NULL)
    {
        log(1, LOG_LEVEL, "Failed to connect to leader\n");
        exit(1);
    }
    freeaddrinfo(servinfo);
    log(0, LOG_LEVEL, "Connected to leader\n");

    if (send(sockfd, buf, sizeof(buf)) == -1)
    {
        log(1, LOG_LEVEL, "Could not send join request to leader\n");
    }
    close(sockfd);

    log(0, LOG_LEVEL, "Join Request Sent\n");

    return;
}

int main(int argc, char *argv[])
{
    // parse the command line
    if (argc < 5)
    {
        log(1, LOG_LEVEL, "USAGE:\n prj2 -p port -h hostfile\n");
    }

    log(0, LOG_LEVEL, "Parsing command line\n");
    if (strcmp(argv[1], "-p") == 0)
    {
        PORT = atoi(argv[2]);
    }
    if (PORT < 10000 || PORT > 65535)
    {
        log(1, LOG_LEVEL, "Port number out of range 10000 - 65535\n");
        exit(1);
    }
    if (strcmp(argv[3], "-h") == 0)
    {
        HOSTS = open_parse_hostfile(argv[4]);
    }
    log(0, LOG_LEVEL, "Command line parsed\n");

    // setup all the socket shit
    log(0, LOG_LEVEL, "Setting up networking\n");
    fd_set master;          // master file descriptor list
    fd_set read_fds;        // temp file descriptor list for select()
    int fdmax;              // maximum file descriptor number

    int listener;           // listening socket descriptor
    int newfd;              // newly accept()ed socket descriptor
    struct sockaddr_storage remoteaddr;     // client address
    socklen_t addrlen;

    char buf[256];          // buffer for client data
    int nbytes;

    char remoteIP[INET6_ADDRSTRLEN];

    int yes=1;              // for setsockopt() SO_REUSEADDR
    int i, j, rv;

    struct addrinfo hints, *ai, *p;

    FD_ZERO(&master);       // clear the master and temp sets
    FD_ZERO(&read_fds);

    // get us a socket and bind it
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0)
    {
        log(1, LOG_LEVEL, "selectserver: %s\n", gai_strerror(rv));
        exit(1);
    }

    for (p = ai; p != NULL; p = p->ai_next)
    {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0)
        {
            continue;
        }
        
        // prevent "address already in use" error message
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0)
        {
            close(listener);
            continue;
        }
        break;
    }

    // if here then didn't bind
    if (p == NULL)
    {
        log(1, LOG_LEVEL, "selectserver: failed to bind\n");
        exit(1);
    }
    freeaddrinfo(ai);

    if (listen(listener, 10) == -1)
    {
        log(1, LOG_LEVEL, "listen fail\n");
        exit(1);
    }

    // add the listener to the master set
    FD_SET(listener, &master);
    // track the biggest file descriptor
    fdmax = listener;

    log(0, LOG_LEVEL, "Networking setup\n");

    // if leader       
        // initialize membership list to include self
        // because we all know all of the members at the start
        // this just an int[] that just references hosts array on send
    if (PROCESS_ID == 1)
    {
        MEMBERSHIP_LIST = malloc(NUM_HOSTS * sizeof(int));
        add_to_membership_list(PROCESS_ID);
    }
    // else   
        // send a message to the leader asking to join
    else
    {
        request_to_join();
    }

    // listening for connections

        // if ready to read

            // get header
                
                // if join request

                    // send req to everyone else
                    // save the operation and list of oks
                
                // if req message

                    // save operation
                    // send ok
                
                // if ok

                    // update ok list for request id and view

                    // if oks list full

                        // increment view id
                        // send New View

                // if new View

                    // update view id
                    // update membership list
                    // print view id and membership list
}