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
#include "failure.h"

#define LOG_LEVEL DEBUG                             // set log level to debug for logging
#define max(A, B) ((A) > (B) ? (A) : (B))
#define True 1
#define False 0
#define Boolean int
#define MAX_OPS 255

char *PORT;
int PROCESS_ID;
char **HOSTS;
int NUM_HOSTS;
uint32_t *MEMBERSHIP_LIST;      
int MEMBERSHIP_SIZE = 0;
Boolean IS_LEADER = False;
int VIEW_ID = 1;
int REQUEST_ID = 1;
StoredOperation **STORED_OPS;
int FAILURE_DETECTOR_SOCKET;
HeartBeat **RECEIVED_HEARTBEATS;

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
    logger(0, LOG_LEVEL, PROCESS_ID, "Got hostname: %s\n", hostname);
    if (cur_hostname == -1)
    {
        logger(1, LOG_LEVEL, PROCESS_ID, "Could not get hostname\n");
        exit(1);
    }

    FILE *fp = fopen(hostfile, "r");
    if (fp == NULL)
    {
        logger(1, LOG_LEVEL, PROCESS_ID, "Could not open hostfile\n");
        exit(1);
    }
    logger(0, LOG_LEVEL, PROCESS_ID, "Opened hostfile\n");

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
    logger(0, LOG_LEVEL, PROCESS_ID, "Hostfile is %d lines\n", numLines);
    fseek(fp, 0, SEEK_SET);     // reset to beginning

    // malloc 2-D array of hostnames
    char **hosts = malloc(numLines * sizeof(char *));
    // keep track of size of host file
    NUM_HOSTS = numLines;
    if (hosts == NULL)
    {
        logger(1, LOG_LEVEL, PROCESS_ID, "Could not malloc hosts array\n");
        exit(1);
    }

    // read in all the host names
    int i = 0;
    for(i = 0; i < NUM_HOSTS; i++)
    {
        hosts[i] = (char *)malloc(255);
        if (hosts[i] == NULL)
        {
            logger(1, LOG_LEVEL, PROCESS_ID, "Error on malloc");
            exit(1);
        }
        fgets(hosts[i], 255, fp);
        char *newline_pos;
        if ((newline_pos=strchr(hosts[i], '\n')) != NULL)
            *newline_pos = '\0';
        logger(0, LOG_LEVEL, PROCESS_ID, "Host %s read in\n", hosts[i]);
        if ((strcmp(hosts[i], hostname)) == 0)
        {
            PROCESS_ID = i+1;
            logger(0, LOG_LEVEL, PROCESS_ID, "Process id: %d\n", PROCESS_ID);
        }
    }

    return hosts;
}

void add_to_membership_list(int process_id)
{
    if (MEMBERSHIP_LIST[process_id-1] == 0)
    {
        MEMBERSHIP_LIST[process_id-1] = process_id;
        logger(0, LOG_LEVEL, PROCESS_ID, "Stored process %d in membership list\n", process_id);
        MEMBERSHIP_SIZE++;
        return;
    }
}

void request_to_join()
{
    Header *header = malloc(sizeof(Header));
    header->msg_type = JoinMessageType;                       // join message
    header->size = sizeof(JoinMessage);

    JoinMessage *join = malloc(sizeof(JoinMessage));
    join->process_id = PROCESS_ID;

    //unsigned char *buf = malloc(sizeof(Header) + sizeof(JoinMessage));
    unsigned char *header_buf = malloc(sizeof(Header));
    unsigned char *join_buf = malloc(sizeof(JoinMessage));

    //pack_header(header, buf);
    //pack_join_message(join, buf+sizeof(Header));             // +8 because need to offset for header
    pack_header(header, header_buf);
    pack_join_message(join, join_buf);
    logger(0, LOG_LEVEL, PROCESS_ID, "Message and header setup and packed\n");

    // since they've been packed we don't need the structs any more
    free(header);
    free(join);

    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(HOSTS[0], PORT, &hints, &servinfo)) != 0)
    {
        logger(1, LOG_LEVEL, PROCESS_ID, "getaddrinfo %s\n", gai_strerror(rv));
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
        logger(1, LOG_LEVEL, PROCESS_ID, "Failed to connect to leader\n");
        exit(1);
    }
    freeaddrinfo(servinfo);
    logger(0, LOG_LEVEL, PROCESS_ID, "Connected to leader\n");

    //if (send(sockfd, buf, sizeof(buf), 0) == -1)
    if (send(sockfd, header_buf, sizeof(Header), 0) == -1)
    {
        logger(1, LOG_LEVEL, PROCESS_ID, "Could not send join request to leader\n");
    }
    send(sockfd, join_buf, sizeof(JoinMessage), 0);
    close(sockfd);

    logger(0, LOG_LEVEL, PROCESS_ID, "Join Request Sent\n");

    // don't need the data anymore
    //free(buf);
    free(header_buf);
    free(join_buf);

    return;
}

void store_operation(ReqMessage *req)
{
    int i;
    for (i = 0; i < MAX_OPS; i++)
    {
        if (STORED_OPS[i] == 0)
        {
            logger(0, LOG_LEVEL, PROCESS_ID, "Found spot for stored op\n");
            STORED_OPS[i] = malloc(sizeof(StoredOperation));

            STORED_OPS[i]->request_id = req->request_id;
            STORED_OPS[i]->curr_view_id = req->curr_view_id;
            STORED_OPS[i]->op_type = req->op_type;
            STORED_OPS[i]->peer_id = req->peer_id;
            STORED_OPS[i]->num_oks = 0;
            //STORED_OPS[i]->num_oks = 1;             // if you are leader then this is helpful
                                                    // for everyone else it's kind of dumb

            logger(0, LOG_LEVEL, PROCESS_ID, "Stored op: %d with view: %d\n", 
                    STORED_OPS[i]->request_id, STORED_OPS[i]->curr_view_id);
            break;
        }
    }
    return;
}

void send_req(JoinMessage *msg)
{
    Header *header = malloc(sizeof(Header));
    header->msg_type = ReqMessageType;                
    header->size = sizeof(ReqMessage);
    
    ReqMessage *req = malloc(sizeof(ReqMessage));
    req->request_id = REQUEST_ID++;
    req->curr_view_id = VIEW_ID;
    req->op_type = OpAdd;
    req->peer_id = msg->process_id;

    logger(0, LOG_LEVEL, PROCESS_ID, "Sending Req\n");
    logger(0, LOG_LEVEL, PROCESS_ID, "\trequest id: %d\n", req->request_id);
    logger(0, LOG_LEVEL, PROCESS_ID, "\tview id: %d\n", req->curr_view_id);
    logger(0, LOG_LEVEL, PROCESS_ID, "\top type: %d\n", req->op_type);
    logger(0, LOG_LEVEL, PROCESS_ID, "\tpeer id: %d\n", req->peer_id);

    // unsigned char *buf = malloc(sizeof(Header) + sizeof(ReqMessage));
    unsigned char *header_buf = malloc(sizeof(Header));
    unsigned char *req_buf = malloc(sizeof(ReqMessage));

    // pack_header(header, buf);
    // pack_req_message(req, buf+8);             // +8 because need to offset for header
    pack_header(header, header_buf);
    pack_req_message(req, req_buf);
    logger(0, LOG_LEVEL, PROCESS_ID, "Message and header setup and packed\n");

    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;

    int i;
    for (i = 0; i < NUM_HOSTS; i++)
    {
        // only need to send to hosts that are members and don't need to send to self
        // if (MEMBERSHIP_LIST[i] == 0 || (i+1) == PROCESS_ID)
        if (MEMBERSHIP_LIST[i] == 0)
        {
            continue;
        }

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        if ((rv = getaddrinfo(HOSTS[i], PORT, &hints, &servinfo)) != 0)
        {
            logger(1, LOG_LEVEL, PROCESS_ID, "getaddrinfo %s\n", gai_strerror(rv));
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
            logger(1, LOG_LEVEL, PROCESS_ID, "Failed to connect to peer\n");
            exit(1);
        }
        freeaddrinfo(servinfo);
        logger(0, LOG_LEVEL, PROCESS_ID, "Connected to peer\n");

        // if (send(sockfd, buf, sizeof(buf), 0) == -1)
        if(send(sockfd, header_buf, sizeof(Header), 0) == -1)
        {
            logger(1, LOG_LEVEL, PROCESS_ID, "Could not send req to peer\n");
        }
        int sent;
        //sent = send(sockfd, req_buf, sizeof(req_buf), 0);
        sent = send(sockfd, req_buf, sizeof(ReqMessage), 0);
        if (sent < sizeof(req_buf))
        {
            logger(0, LOG_LEVEL, PROCESS_ID, "Only sent %d instead of %d\n", sent, sizeof(req_buf));
        }
        logger(0, LOG_LEVEL, PROCESS_ID, "Sent %d bytes of req message\n", sent);
        logger(0, LOG_LEVEL, PROCESS_ID, "%d\n", unpacki32(req_buf+8));
        close(sockfd);

    }
    logger(0, LOG_LEVEL, PROCESS_ID, "Reqs Sent\n");

    // we can go ahead and store it and we know we will ok it.
    //store_operation(req);

    // since they've been packed we don't need the structs any more
    free(header);
    free(req);
    // free(buf);
    free(header_buf);
    free(req_buf);

    return;
}

void send_ok(ReqMessage *req)
{
    Header *header = malloc(sizeof(Header));
    header->msg_type = OkMessageType;                
    header->size = sizeof(OkMessage);
    
    OkMessage *ok = malloc(sizeof(OkMessage));
    ok->request_id = req->request_id;
    ok->curr_view_id = req->curr_view_id;

    // unsigned char *buf = malloc(sizeof(Header) + sizeof(OkMessage));
    unsigned char *header_buf = malloc(sizeof(Header));
    unsigned char *ok_buf = malloc(sizeof(OkMessage));

    // pack_header(header, buf);
    // pack_ok_message(ok, buf+8);             // +8 because need to offset for header
    pack_header(header, header_buf);
    pack_ok_message(ok, ok_buf);
    logger(0, LOG_LEVEL, PROCESS_ID, "Message and header setup and packed\n");

    // since they've been packed we don't need the structs any more
    free(header);
    free(ok);

    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(HOSTS[0], PORT, &hints, &servinfo)) != 0)
    {
        logger(1, LOG_LEVEL, PROCESS_ID, "getaddrinfo %s\n", gai_strerror(rv));
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
        logger(1, LOG_LEVEL, PROCESS_ID, "Failed to connect to leader\n");
        exit(1);
    }
    freeaddrinfo(servinfo);
    logger(0, LOG_LEVEL, PROCESS_ID, "Connected to leader\n");

    // if (send(sockfd, buf, sizeof(buf), 0) == -1)
    if (send(sockfd, header_buf, sizeof(Header), 0) == -1)
    {
        logger(1, LOG_LEVEL, PROCESS_ID, "Could not send ok to leader\n");
    }
    send(sockfd, ok_buf, sizeof(OkMessage), 0);
    close(sockfd);

    logger(0, LOG_LEVEL, PROCESS_ID, "Ok Sent\n");

    // don't need the data anymore
    // free(buf);
    free(header_buf);
    free(ok_buf);

    return;
}

void send_new_view()
{   
    Header *header = malloc(sizeof(Header));
    header->msg_type = NewViewMessageType;  
    // view_id + membership_size + all_members              
    header->size = (2 * sizeof(uint32_t)) + (MEMBERSHIP_SIZE * sizeof(uint32_t));
    logger(0, LOG_LEVEL, PROCESS_ID, "New View Message size %d\n", header->size);
    
    NewViewMessage *view = malloc(sizeof(NewViewMessage));
    view->view_id = VIEW_ID;
    view->membership_size = MEMBERSHIP_SIZE;
    view->membership_list = MEMBERSHIP_LIST;

    // room for header + view_id + membership_size + all_members
    // unsigned char *buf = malloc(sizeof(Header) + (2 * sizeof(uint32_t)) + 
                        // (view->membership_size * sizeof(int)));
    unsigned char *header_buf = malloc(sizeof(Header));
    unsigned char *new_view_buf = malloc(header->size);

    // pack_header(header, buf);
    pack_header(header, header_buf);
    logger(0, LOG_LEVEL, PROCESS_ID, "Packing new view\n");
    // pack_view_message(view, buf+8);             // +8 because need to offset for header
    pack_view_message(view, new_view_buf);
    logger(0, LOG_LEVEL, PROCESS_ID, "Finished packing new view\n");
    logger(0, LOG_LEVEL, PROCESS_ID, "Message and header setup and packed\n");

    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;

    int i;
    for (i = 0; i < NUM_HOSTS; i++)
    {
        // only need to send to hosts that are members and don't need to send to self
        //if (MEMBERSHIP_LIST[i] == 0 || (i+1) == PROCESS_ID)
        if (MEMBERSHIP_LIST[i] == 0)
        {
            continue;
        }

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        if ((rv = getaddrinfo(HOSTS[i], PORT, &hints, &servinfo)) != 0)
        {
            logger(1, LOG_LEVEL, PROCESS_ID, "getaddrinfo %s\n", gai_strerror(rv));
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
            logger(1, LOG_LEVEL, PROCESS_ID, "Failed to connect to peer\n");
            exit(1);
        }
        freeaddrinfo(servinfo);
        logger(0, LOG_LEVEL, PROCESS_ID, "Connected to peer\n");

        // if (send(sockfd, buf, sizeof(buf), 0) == -1)
        if (send(sockfd, header_buf, sizeof(Header), 0) == -1)
        {
            logger(1, LOG_LEVEL, PROCESS_ID, "Could not send new_view to peer\n");
        }
        send(sockfd, new_view_buf, header->size, 0);
        close(sockfd);

    }
    logger(0, LOG_LEVEL, PROCESS_ID, "New View Sent\n");
    logger(0, LOG_LEVEL, PROCESS_ID, "\tview id: %08x\n", unpacki32(new_view_buf));
    logger(0, LOG_LEVEL, PROCESS_ID, "\tmembership size: %08x\n", unpacki32(new_view_buf+4));

    // since they've been packed we don't need the structs any more
    free(header);
    free(view);
    // free(buf);
    free(header_buf);
    free(new_view_buf);

    return;
}

int main(int argc, char *argv[])
{
    // parse the command line
    if (argc < 5)
    {
        logger(1, LOG_LEVEL, PROCESS_ID, "USAGE:\n prj2 -p port -h hostfile\n");
    }

    logger(0, LOG_LEVEL, PROCESS_ID, "Parsing command line\n");
    if (strcmp(argv[1], "-p") == 0)
    {
        PORT = argv[2];
    }
    if (atoi(PORT) < 10000 || atoi(PORT) > 65535)
    {
        logger(1, LOG_LEVEL, PROCESS_ID, "Port number out of range 10000 - 65535\n");
        exit(1);
    }
    if (strcmp(argv[3], "-h") == 0)
    {
        HOSTS = open_parse_hostfile(argv[4]);
    }
    logger(0, LOG_LEVEL, PROCESS_ID, "Command line parsed\n");

    STORED_OPS = malloc(MAX_OPS * sizeof(StoredOperation *));
    memset(STORED_OPS, 0, sizeof(*STORED_OPS));
    logger(0, LOG_LEVEL, PROCESS_ID, "Setup stored operations\n");

    // setup all the socket shit
    logger(0, LOG_LEVEL, PROCESS_ID, "Setting up networking\n");
    fd_set master;          // master file descriptor list
    fd_set read_fds;        // temp file descriptor list for select()
    int fdmax;              // maximum file descriptor number

    fd_set failure_master;  // master file descriptor for failure detector
    fd_set failure_tmp;     // file descriptor list for select() failure detector

    int listener;           // listening socket descriptor
    int newfd;              // newly accept()ed socket descriptor
    struct sockaddr_storage remoteaddr;     // client address
    socklen_t addrlen;

    int nbytes;

    int yes=1;              // for setsockopt() SO_REUSEADDR
    int i, rv;

    struct addrinfo hints, *ai, *p;

    FD_ZERO(&master);       // clear the master and temp sets
    FD_ZERO(&read_fds);

    // bind the failure detector
    FAILURE_DETECTOR_SOCKET = bind_failure_detector();
    // zero out and set the file descriptor sets
    FD_ZERO(&failure_master);
    FD_ZERO(&failure_tmp);
    FD_SET(FAILURE_DETECTOR_SOCKET, &failure_master);

    // initialize keeping track of when heartbeats are received
    // if process has not sent a heartbeat then it is NULL
    RECEIVED_HEARTBEATS = malloc(NUM_HOSTS * sizeof(ReceivedHeartBeat));
    int j;
    for (j = 0; j < NUM_HOSTS; j++)
    {
        RECEIVED_HEARTBEATS[j] = malloc(sizeof(ReceivedHeartBeat));
        RECEIVED_HEARTBEATS[j]->recvd_time = NULL;
    }

    // get us a socket and bind it
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0)
    {
        logger(1, LOG_LEVEL, PROCESS_ID, "selectserver: %s\n", gai_strerror(rv));
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
        logger(1, LOG_LEVEL, PROCESS_ID, "selectserver: failed to bind\n");
        exit(1);
    }
    freeaddrinfo(ai);

    if (listen(listener, 10) == -1)
    {
        logger(1, LOG_LEVEL, PROCESS_ID, "listen fail\n");
        exit(1);
    }

    // add the listener to the master set
    FD_SET(listener, &master);
    // track the biggest file descriptor
    fdmax = listener;

    logger(0, LOG_LEVEL, PROCESS_ID, "Networking setup\n");

    // if leader       
        // initialize membership list to include self
        // because we all know all of the members at the start
        // this just an int[] that just references hosts array on send
    MEMBERSHIP_LIST = malloc(NUM_HOSTS * sizeof(int));
    memset(MEMBERSHIP_LIST, 0, sizeof(*MEMBERSHIP_LIST));
    if (PROCESS_ID == 1)
    {
        IS_LEADER = True;
        // MEMBERSHIP_LIST = malloc(NUM_HOSTS * sizeof(int));
        // memset(MEMBERSHIP_LIST, 0, sizeof(*MEMBERSHIP_LIST));
        add_to_membership_list(PROCESS_ID);
    }
    // else   
        // send a message to the leader asking to join
    else
    {
        logger(0, LOG_LEVEL, PROCESS_ID, "Messaging leader to join\n");
        request_to_join();
    }

    // declare timeout for select
    struct timeval select_timeout;

    // listening for connections
    for (;;)
    {
        read_fds = master;      // copy fd set
        failure_tmp = failure_master;
    
        // setup timeout for select to 2.5 sec, some unixes change this everytime
        select_timeout.tv_sec = 2;
        select_timeout.tv_usec = 500000;
    
        // select for failure detector and get the heartbeat if there is one
        if (select(FAILURE_DETECTOR_SOCKET+1, &failure_tmp, NULL, NULL, &select_timeout) == -1)
        {
            logger(1, LOG_LEVEL, PROCESS_ID, "Failed on select failure det\n");
            exit(1);
        }
        if (FD_ISSET(FAILURE_DETECTOR_SOCKET, &failure_tmp))
        {
            logger(0, LOG_LEVEL, PROCESS_ID, "Received HeartBeat\n");
            int success = get_heartbeat(FAILURE_DETECTOR_SOCKET);
        }

        // check which process have died
        for (j = 0; j < NUM_HOSTS; j++)
        {
            if (RECEIVED_HEARTBEATS[j]->recvd_time == NULL)
            {
                continue;
            }

            struct timeval cur_time;
            gettimeofday(&cur_time, NULL);

            // timeout is 2.5 for heart beats so if we've waited more than 2x that
            if ((cur_time.tv_sec - RECEIVED_HEARTBEATS[j]->recvd_time->tv_sec) >= 5)
            {
                printf("Peer %d not reachable\n", j+1);
                RECEIVED_HEARTBEATS[j]->recvd_time = NULL;
            }
        }

        // send heartbeat
        send_heartbeat(PROCESS_ID);

        select_timeout.tv_sec = 2;
        select_timeout.tv_usec = 500000;

        if (select(fdmax+1, &read_fds, NULL, NULL, &select_timeout) == -1)
        {
            logger(1, LOG_LEVEL, PROCESS_ID, "Failed on select tcp listener\n");
            exit(1);
        }

        for (i = 0; i <= fdmax; i++)
        {
            // Something to read on this socket
            if (FD_ISSET(i, &read_fds))
            {
                // handle new connection
                if (i == listener)
                {
                    addrlen = sizeof(remoteaddr);
                    newfd = accept(listener, (struct sockaddr *)&remoteaddr, &addrlen);
                    
                    if (newfd == -1)
                    {
                        logger(1, LOG_LEVEL, PROCESS_ID, "Fail on accept\n");
                    }
                    else
                    {
                        FD_SET(newfd, &master);         // add to master set
                        fdmax = max(fdmax, newfd);      // keep track of max
                    }
                }
                // handle data from a client
                else
                {
                    unsigned char *recvd_header = malloc(sizeof(Header));
                    Header *header = malloc(sizeof(Header));

                    // get header
                    if ((nbytes = recv(i, recvd_header, sizeof(recvd_header), 0)) <= 0)
                    {
                        // error or connection closed
                        close(i);
                        FD_CLR(i, &master);         // remove from master set
                    }
                    else
                    {
                        unpack_header(header, recvd_header);        // unpack the header

                        switch(header->msg_type)
                        {
                            // if join request
                            case JoinMessageType:
                                logger(0, LOG_LEVEL, PROCESS_ID, "Received Join Request\n");
                                JoinMessage *join = malloc(sizeof(JoinMessage));
                                unsigned char *join_buf = malloc(header->size);
                                if ((nbytes = recv(i, join_buf, header->size, 0)) <= 0)
                                {
                                    // error on receiving join message
                                    close(i);
                                    FD_CLR(i, &master);
                                }
                                unpack_join_message(join, join_buf);

                                // send req to everyone else
                                send_req(join);
                
                                // free everything
                                free(join);
                                free(join_buf);
                                break;
                            // if req message
                            case ReqMessageType:
                                logger(0, LOG_LEVEL, PROCESS_ID, "Received Req Message\n");
                                ReqMessage *req = malloc(sizeof(ReqMessage));
                                unsigned char *req_buf = malloc(header->size);
                                //if ((nbytes = recv(i, req_buf, header->size, 0)) <= 0)
                                if ((nbytes = recv(i, req_buf, header->size, 0)) < header->size)
                                {
                                    close(i);
                                    logger(0, LOG_LEVEL, PROCESS_ID, "Didn't get full message\n");
                                    FD_CLR(i, &master);
                                }
                                unpack_req_message(req, req_buf);

                                logger(0, LOG_LEVEL, PROCESS_ID, "\trequest id: %08x\n", req->request_id);
                                logger(0, LOG_LEVEL, PROCESS_ID, "\tview id: %08x\n", req->curr_view_id);
                                logger(0, LOG_LEVEL, PROCESS_ID, "\top type: %08x\n", req->op_type);
                                logger(0, LOG_LEVEL, PROCESS_ID, "\tpeer id: %08x\n", req->peer_id);

                                // save operation
                                store_operation(req);
                                // send ok
                                send_ok(req);

                                free(req);
                                free(req_buf);
                                break;
                            // if ok
                            case OkMessageType:
                                logger(0, LOG_LEVEL, PROCESS_ID, "Received Ok Message\n");
                                OkMessage *ok = malloc(sizeof(OkMessage));
                                unsigned char *ok_buf = malloc(header->size);
                                if ((nbytes = recv(i, ok_buf, header->size, 0)) <= 0)
                                {
                                    close(i);
                                    FD_CLR(i, &master);
                                }
                                unpack_ok_message(ok, ok_buf);
                                logger(0, LOG_LEVEL, PROCESS_ID, "OK unpacked\n");
                                // update ok list for request id and view
                                int j;
                                for (j = 0; j < MAX_OPS; j++)
                                {
                                    if (STORED_OPS[j] == 0)
                                    {
                                        continue;
                                        logger(0, LOG_LEVEL, PROCESS_ID, "Empty stored op\n");
                                    }
                                    if (STORED_OPS[j]->request_id == ok->request_id && 
                                            STORED_OPS[j]->curr_view_id == ok->curr_view_id)
                                    {
                                        //STORED_OPS[i]->num_oks ++;
                                        logger(0, LOG_LEVEL, PROCESS_ID, "Found stored message\n");
                                        STORED_OPS[j]->num_oks = STORED_OPS[j]->num_oks + 1;
                                        break;
                                    }
                                }

                                // if received all oks
                                if (STORED_OPS[j]->num_oks == MEMBERSHIP_SIZE)
                                {
                                    // add peer to your membership list
                                    add_to_membership_list(STORED_OPS[j]->peer_id);
                                    //increment view id
                                    VIEW_ID++;

                                    // send new view
                                    send_new_view();
                                }
                                else
                                {
                                    logger(0, LOG_LEVEL, PROCESS_ID, "Have %d oks\n", STORED_OPS[j]->num_oks);
                                }

                                free(ok);
                                free(ok_buf);
                                break;
                            // if new view
                            case NewViewMessageType:
                                logger(0, LOG_LEVEL, PROCESS_ID, "Received New view message\n");
                                logger(0, LOG_LEVEL, PROCESS_ID, "Header says size is %08x\n", header->size);
                                NewViewMessage *view = malloc(sizeof(NewViewMessage));
                                unsigned char *view_buf = malloc(header->size);
                                if ((nbytes = recv(i, view_buf, header->size, 0)) == -1)
                                {
                                    logger(1, LOG_LEVEL, PROCESS_ID, "Did not receive full view\n");
                                    logger(1, LOG_LEVEL, PROCESS_ID, "recv: %s (%d)\n", strerror(errno), errno);
                                    logger(1, LOG_LEVEL, PROCESS_ID, "Socket %d\n", i);
                                    close(i);
                                    FD_CLR(i, &master);
                                }
                                logger(0, LOG_LEVEL, PROCESS_ID, "Unpacking new view\n");
                                unpack_view_message(view, view_buf);
                                logger(0, LOG_LEVEL, PROCESS_ID, "Finished unpacking new view\n");

                                // update view id
                                VIEW_ID = view->view_id;
                                logger(0, LOG_LEVEL, PROCESS_ID, "Updated View id\n");
                                // update membership list
                                for (j = 0; j < view->membership_size; j++)
                                {
                                    add_to_membership_list(view->membership_list[j]);
                                }
                                logger(0, LOG_LEVEL, PROCESS_ID, "Updated Membership list\n");
                                // print view id and membership list
                                printf("View: %d\n", VIEW_ID);
                                printf("Members\n");
                                for (j = 0; j < NUM_HOSTS; j++)
                                {
                                    if (MEMBERSHIP_LIST[j] != 0)
                                    {
                                        printf("\t%d\n", MEMBERSHIP_LIST[j]);
                                    }
                                }
                                free(view->membership_list);
                                free(view);
                                free(view_buf);
                                break;
                            default:
                                break;
                        }
                    }

                    free(recvd_header);
                    free(header);
                }
            }
        }
    }

}
