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
// StoredOperation **STORED_OPS;
StoredOperation *STORED_OP;
int FAILURE_DETECTOR_SOCKET;
ReceivedHeartBeat **RECEIVED_HEARTBEATS;
time_t LAST_HEARTBEAT_SENT;
int LEADER_ID = 1;              // default is that first peer in host list is leader

// for testing purposes we can turn off different functionality by assigning these values
// using the -t switch. The default is for them to all be true, ie everything turned on.
Boolean TEST2 = True;
Boolean TEST3 = True;
Boolean TEST4 = True;

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

void edit_membership_list(int process_id, uint32_t op_type)
{
    if (op_type == OpAdd)
    {
        if (MEMBERSHIP_LIST[process_id-1] == 0)
        {
            MEMBERSHIP_LIST[process_id-1] = process_id;
            logger(0, LOG_LEVEL, PROCESS_ID, "Stored process %d in membership list\n", process_id);
            MEMBERSHIP_SIZE++;
            // logger(0, LOG_LEVEL, PROCESS_ID, "Membership list is of size %d\n", MEMBERSHIP_SIZE);
            return;
        }
    }
    if (op_type == OpDel)
    {
        if (MEMBERSHIP_LIST[process_id-1] != 0)
        {
            MEMBERSHIP_LIST[process_id-1] = 0;
            logger(0, LOG_LEVEL, PROCESS_ID, "Removed process %d from membership\n", process_id);
            MEMBERSHIP_SIZE--;
            // logger(0, LOG_LEVEL, PROCESS_ID, "Membership list is of size %d\n", MEMBERSHIP_SIZE);
            return;
        }
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

    if ((rv = getaddrinfo(HOSTS[LEADER_ID-1], PORT, &hints, &servinfo)) != 0)
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
    // int i;
    // for (i = 0; i < MAX_OPS; i++)
    // {
    //     if (STORED_OPS[i] == 0)
    //     {
    //         logger(0, LOG_LEVEL, PROCESS_ID, "Found spot for stored op\n");
    //         STORED_OPS[i] = malloc(sizeof(StoredOperation));

    //         STORED_OPS[i]->request_id = req->request_id;
    //         STORED_OPS[i]->curr_view_id = req->curr_view_id;
    //         STORED_OPS[i]->op_type = req->op_type;
    //         STORED_OPS[i]->peer_id = req->peer_id;
    //         STORED_OPS[i]->num_oks = 0;
    //         //STORED_OPS[i]->num_oks = 1;             // if you are leader then this is helpful
    //                                                 // for everyone else it's kind of dumb

    //         logger(0, LOG_LEVEL, PROCESS_ID, "Stored op: %d with view: %d\n", 
    //                 STORED_OPS[i]->request_id, STORED_OPS[i]->curr_view_id);
    //         break;
    //     }
    // }
    // return;
    STORED_OP = (StoredOperation *)malloc(sizeof(StoredOperation));
    STORED_OP->request_id = req->request_id;
    STORED_OP->curr_view_id = req->curr_view_id;
    STORED_OP->op_type = req->op_type;
    STORED_OP->peer_id = req->peer_id;
    STORED_OP->num_oks = 0;

    logger(0, LOG_LEVEL, PROCESS_ID, "Stored op: %d with view %d\n", 
        STORED_OP->request_id, STORED_OP->curr_view_id);

    return;
}

//void send_req(JoinMessage *msg)
void send_req(uint32_t peer_id, uint32_t op_type)
{
    Header *header = malloc(sizeof(Header));
    header->msg_type = ReqMessageType;                
    header->size = sizeof(ReqMessage);
    
    ReqMessage *req = malloc(sizeof(ReqMessage));
    req->request_id = REQUEST_ID++;
    req->curr_view_id = VIEW_ID;
    // req->op_type = OpAdd;
    // req->peer_id = msg->process_id;
    req->op_type = op_type;
    req->peer_id = peer_id;

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

        logger(0, LOG_LEVEL, PROCESS_ID, "Peer %d in Membership list is %08x\n", i, MEMBERSHIP_LIST[i]);
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
            logger(1, LOG_LEVEL, PROCESS_ID, "Failed to connect to peer %d\n", i+1);
            exit(1);
        }
        freeaddrinfo(servinfo);
        logger(0, LOG_LEVEL, PROCESS_ID, "Connected to peer %d\n", i+1);

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
        //logger(0, LOG_LEVEL, PROCESS_ID, "Sent %d bytes of req message\n", sent);
        //logger(0, LOG_LEVEL, PROCESS_ID, "%d\n", unpacki32(req_buf+8));
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

void send_pending_op()
{
    Header *header = malloc(sizeof(Header));
    header->msg_type = PendingOpType;                
    header->size = sizeof(PendingOp);

    PendingOp *pending = malloc(sizeof(PendingOp));
    if (STORED_OP != 0)
    {
        pending->request_id = STORED_OP->request_id;
        pending->curr_view_id = VIEW_ID;
        pending->op_type = STORED_OP->op_type;
        pending->peer_id = STORED_OP->peer_id;
    }
    else 
    {
        pending->request_id = 0;
        pending->curr_view_id = VIEW_ID;
        pending->op_type = OpNothing;
        pending->peer_id = 0;
    }

    unsigned char *header_buf = malloc(sizeof(Header));
    unsigned char *pending_buf = malloc(sizeof(PendingOp));

    pack_header(header, header_buf);
    pack_pending_op(pending, pending_buf);
    logger(0, LOG_LEVEL, PROCESS_ID, "Message and header setup and packed\n");

    // since they've been packed we don't need the structs any more
    free(header);
    free(pending);

    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(HOSTS[LEADER_ID-1], PORT, &hints, &servinfo)) != 0)
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
    send(sockfd, pending_buf, sizeof(OkMessage), 0);
    close(sockfd);

    logger(0, LOG_LEVEL, PROCESS_ID, "Ok Sent\n");

    // don't need the data anymore
    // free(buf);
    free(header_buf);
    free(pending_buf);

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

void send_new_leader_msg()
{
    Header *header = malloc(sizeof(Header));
    header->msg_type = NewLeaderMessageType;  
    // view_id + membership_size + all_members              
    header->size = sizeof(NewLeaderMessage);

    NewLeaderMessage *leader = malloc(sizeof(NewLeaderMessage));
    leader->request_id = REQUEST_ID++;
    leader->curr_view_id = VIEW_ID;
    leader->op_type = OpPending;

    unsigned char *header_buf = malloc(sizeof(Header));
    unsigned char *new_leader_buf = malloc(header->size);

    // pack_header(header, buf);
    pack_header(header, header_buf);
    pack_new_leader(leader, new_leader_buf);

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
        send(sockfd, new_leader_buf, header->size, 0);
        close(sockfd);

    }
    // since they've been packed we don't need the structs any more
    free(header);
    free(leader);
    // free(buf);
    free(header_buf);
    free(new_leader_buf);

    return;
}

int main(int argc, char *argv[])
{
    // parse the command line
    if (argc < 5)
    {
        logger(1, LOG_LEVEL, PROCESS_ID, "USAGE:\n prj2 -p port -h hostfile\n");
        exit(1);
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

    if (argc == 7)
    {
        if (strcmp(argv[5], "-t") == 0)
        {
            int test_level = atoi(argv[6]);
            logger(0, LOG_LEVEL, PROCESS_ID, "Testing level %d\n", test_level);
            switch (test_level)
            {
                case 1:
                    TEST2 = False;
                    TEST3 = False;
                    TEST4 = False;
                    break;
                case 2:
                    TEST3 = False;
                    TEST4 = False;
                    break;
                case 3:
                    TEST4 = False;
                    break;
                case 4:
                    break;
                default:
                    break;
            }
        }
    }

    logger(0, LOG_LEVEL, PROCESS_ID, "Command line parsed\n");

    // STORED_OPS = malloc(MAX_OPS * sizeof(StoredOperation *));
    // memset(STORED_OPS, 0, sizeof(*STORED_OPS));
    // logger(0, LOG_LEVEL, PROCESS_ID, "Setup stored operations\n");
    STORED_OP = 0;
    logger(0, LOG_LEVEL, PROCESS_ID, "Setup stored operation\n");

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
    //memset(MEMBERSHIP_LIST, 0, sizeof(*MEMBERSHIP_LIST));
    for (j = 0; j < NUM_HOSTS; j++)
    {
        MEMBERSHIP_LIST[j] = 0;
    }
    if (PROCESS_ID == 1)
    {
        IS_LEADER = True;
        // MEMBERSHIP_LIST = malloc(NUM_HOSTS * sizeof(int));
        // memset(MEMBERSHIP_LIST, 0, sizeof(*MEMBERSHIP_LIST));
        edit_membership_list(PROCESS_ID, OpAdd);
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

    struct timeval cur_time;
    gettimeofday(&cur_time, NULL);

    LAST_HEARTBEAT_SENT = cur_time.tv_sec;

    // listening for connections
    for (;;)
    {
        read_fds = master;      // copy fd set
        
        // Only do the failure detector stuff if you're test2 or higher
        if (TEST2)
        {
            failure_tmp = failure_master;
            select_timeout.tv_sec = 0;
            select_timeout.tv_usec = 500000;
        
            // select for failure detector and get the heartbeat if there is one
            if (select(FAILURE_DETECTOR_SOCKET+1, &failure_tmp, NULL, NULL, &select_timeout) == -1)
            {
                logger(1, LOG_LEVEL, PROCESS_ID, "Failed on select failure det\n");
                exit(1);
            }
            //if (FD_ISSET(FAILURE_DETECTOR_SOCKET, &failure_tmp))
            while (FD_ISSET(FAILURE_DETECTOR_SOCKET, &failure_tmp))
            {
                logger(0, LOG_LEVEL, PROCESS_ID, "Received HeartBeat\n");
                get_heartbeat(FAILURE_DETECTOR_SOCKET);
                // select again because heartbeats get stacked up
                failure_tmp = failure_master;
                select_timeout.tv_sec = 0;
                select_timeout.tv_usec = 500000;
                select(FAILURE_DETECTOR_SOCKET+1, &failure_tmp, NULL, NULL, &select_timeout);
            }

            // check which process have died
            for (j = 0; j < NUM_HOSTS; j++)
            {
                //logger(0, LOG_LEVEL, PROCESS_ID, "Verifying for %d\n", j+1);
                //if (RECEIVED_HEARTBEATS[j]->recvd_time == NULL)
                //if (MEMBERSHIP_LIST[j] == 0 || (j+1) == PROCESS_ID)
                if (MEMBERSHIP_LIST[j] == 0 || RECEIVED_HEARTBEATS[j]->recvd_time == NULL)
                {
                    continue;
                }

                gettimeofday(&cur_time, NULL);

                // XXX if you are the leader and peer not reachable then you need to send
                // del req
                // timeout is 2.5 for heart beats so if we've waited more than 2x that
                if ((cur_time.tv_sec - RECEIVED_HEARTBEATS[j]->recvd_time->tv_sec) >= 4)
                {
                    printf("Peer %d not reachable\n", j+1);
                    logger(1, LOG_LEVEL, PROCESS_ID, "Peer %d not reachable\n", j+1);
                    fflush(stdout);
                    RECEIVED_HEARTBEATS[j]->recvd_time = NULL;

                    // if test3 or higher then change membership list
                    if (TEST3)
                    {
                        if (IS_LEADER)
                        // TODO you also need to remove that peer from your own membership
                        // list or else the networking will fail
                        {
                            edit_membership_list(j+1, OpDel);
                            send_req(j+1, OpDel);
                        }

                        // only select a new leader if test4
                        if (TEST4)
                        {
                            // if the leader has crashed
                            // go through membership list and leader is now lowest id that is member
                            if (j+1 == LEADER_ID)
                            {
                                logger(0, LOG_LEVEL, PROCESS_ID, "Detected leader crashed\n");
                                MEMBERSHIP_LIST[j] = 0;
                                int k;
                                for (k = 0; k < NUM_HOSTS; k++)
                                {
                                    if (MEMBERSHIP_LIST[k] != 0)
                                    {
                                        logger(0, LOG_LEVEL, PROCESS_ID, "New leader is %d\n", k);
                                        LEADER_ID = k;
                                        // if you are new leader
                                        if (LEADER_ID == PROCESS_ID)
                                        {
                                            IS_LEADER = True;
                                            logger(0, LOG_LEVEL, PROCESS_ID, 
                                                    "Sending new leader message\n");
                                            send_new_leader_msg();
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // send heartbeat
            gettimeofday(&cur_time, NULL);
            if (cur_time.tv_sec - LAST_HEARTBEAT_SENT > 2)
            {
                send_heartbeat(PROCESS_ID);
                LAST_HEARTBEAT_SENT = cur_time.tv_sec;
            }
        }

        select_timeout.tv_sec = 0;
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
                                // send_req(join);
                                send_req(join->process_id, OpAdd);
                
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

                                // for test4, new leader will ignore a join to test restarting
                                // a pending op
                                if (TEST4 && (PROCESS_ID == 2) && (req->request_id == 3))
                                {
                                    ;   // Do nothing
                                }
                                else        // Normal behavior
                                {
                                    // save operation
                                    store_operation(req);
                                    // send ok
                                    send_ok(req);
                                }

                                free(req);
                                free(req_buf);
                                break;
                            // TODO Oks need to be checked based on request/view id
                            // different ops need to trigger different sends
                            // if ok
                            // only the leader should be receiving oks
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
                                // int j;
                                // for (j = 0; j < MAX_OPS; j++)
                                // {
                                //     if (STORED_OPS[j] == 0)
                                //     {
                                //         continue;
                                //         logger(0, LOG_LEVEL, PROCESS_ID, "Empty stored op\n");
                                //     }
                                //     if (STORED_OPS[j]->request_id == ok->request_id && 
                                //             STORED_OPS[j]->curr_view_id == ok->curr_view_id)
                                //     {
                                //         //STORED_OPS[i]->num_oks ++;
                                //         logger(0, LOG_LEVEL, PROCESS_ID, "Found stored message\n");
                                //         STORED_OPS[j]->num_oks = STORED_OPS[j]->num_oks + 1;
                                //         break;
                                //     }
                                // }

                                if (STORED_OP->request_id == ok->request_id &&
                                    STORED_OP->curr_view_id == ok->curr_view_id)
                                {
                                    logger(0, LOG_LEVEL, PROCESS_ID, "Matched pending operation\n");
                                    STORED_OP->num_oks = STORED_OP->num_oks + 1;
                                }
                                else
                                {
                                    logger(1, LOG_LEVEL, PROCESS_ID, 
                                        "Can only handle one pending op at a time\n");
                                    exit(1);
                                }

                                // FIXME this needs to handle both adding and removing from
                                // membership list based on the op type
                                // if received all oks
                                // if (STORED_OPS[j]->num_oks == MEMBERSHIP_SIZE)
                                // {
                                //     if (STORED_OPS[j]->op_type == OpAdd)
                                //     {
                                //         // add peer to your membership list
                                //         edit_membership_list(STORED_OPS[j]->peer_id, OpAdd);
                                //     }
                                //     else if (STORED_OPS[j]->op_type == OpDel)
                                //     {
                                //         edit_membership_list(STORED_OPS[j]->peer_id, OpDel);
                                //     }

                                //     //increment view id
                                //     VIEW_ID++;
                                //     // send new view
                                //     send_new_view();
                                // }

                                if (STORED_OP->num_oks == MEMBERSHIP_SIZE)
                                {
                                    if (STORED_OP->op_type == OpAdd)
                                    {
                                        edit_membership_list(STORED_OP->peer_id, OpAdd);
                                    }
                                    else if (STORED_OP->op_type == OpDel)
                                    {
                                        edit_membership_list(STORED_OP->peer_id, OpDel);
                                    }

                                    STORED_OP = 0;
                                    VIEW_ID++;
                                    send_new_view();
                                }
                                // else
                                // {
                                //     logger(0, LOG_LEVEL, PROCESS_ID, "Have %d oks\n", STORED_OPS[j]->num_oks);
                                // }

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
                                // FIXME this needs to handle both adding and removing from 
                                // membership list
                                // XXX probably easiest just to clear out the membership list
                                // and then re initialize based on the new view

                                // clear membership list
                                for (j = 0; j < NUM_HOSTS; j++)
                                {
                                    MEMBERSHIP_LIST[j] = 0;
                                }
                                MEMBERSHIP_SIZE = 0;
                                // readd everything to membership list  
                                for (j = 0; j < view->membership_size; j++)
                                {
                                    edit_membership_list(view->membership_list[j], OpAdd);
                                }
                                logger(0, LOG_LEVEL, PROCESS_ID, "Updated Membership list\n");
                                // print view id and membership list
                                printf("View: %d\n", VIEW_ID);
                                logger(1, LOG_LEVEL, PROCESS_ID, "View: %d\n", VIEW_ID);
                                printf("Members\n");
                                logger(1, LOG_LEVEL, PROCESS_ID, "Members\n");
                                for (j = 0; j < NUM_HOSTS; j++)
                                {
                                    if (MEMBERSHIP_LIST[j] != 0)
                                    {
                                        printf("\t%d\n", MEMBERSHIP_LIST[j]);
                                        logger(1, LOG_LEVEL, PROCESS_ID, "\t%d\n", MEMBERSHIP_LIST[j]);
                                    }
                                }

                                // nothing pending anymore
                                free(STORED_OP);
                                STORED_OP = 0;

                                // trigger them to resend heartbeats when someone new joins
                                // sometimes they still think someone has failed who is still
                                // alive
                                LAST_HEARTBEAT_SENT = 0;

                                fflush(stdout);

                                free(view->membership_list);
                                free(view);
                                free(view_buf);
                                break;

                            case NewLeaderMessageType:
                                logger(0, LOG_LEVEL, PROCESS_ID, "Received New Leader Message\n");
                                NewLeaderMessage *leader = malloc(sizeof(NewLeaderMessage));
                                unsigned char *leader_buf = malloc(header->size);
                                if ((nbytes = recv(i, leader_buf, header->size, 0)) <= 0)
                                {
                                    close(i);
                                    FD_CLR(i, &master);
                                }
                                unpack_new_leader(leader, leader_buf);
                                logger(0, LOG_LEVEL, PROCESS_ID, "New Leader unpacked\n");

                                logger(0, LOG_LEVEL, PROCESS_ID, "Sending pending op\n");
                                send_pending_op();
                                break;

                            case PendingOpType:
                                logger(0, LOG_LEVEL, PROCESS_ID, "Received Pending Op\n");
                                PendingOp *pending = malloc(sizeof(PendingOp));
                                unsigned char *pending_buf = malloc(header->size);
                                if ((nbytes = recv(i, pending_buf, header->size, 0)) <= 0)
                                {
                                    close(i);
                                    FD_CLR(i, &master);
                                }
                                unpack_pending_op(pending, pending_buf);
                                logger(0, LOG_LEVEL, PROCESS_ID, "Pending Op unpacked\n");

                                if (pending->op_type == OpNothing)
                                {
                                    break;
                                }
                                else if (pending->op_type == OpAdd)
                                {
                                    // should just resned the request, restarting the 2pc protocol
                                    // edit_membership_list(pending->peer_id, OpAdd);
                                    send_req(pending->peer_id, OpAdd);
                                }
                                else if (pending->op_type == OpDel)
                                {
                                    // edit_membership_list(pending->peer_id, OpDel);
                                    send_req(pending->peer_id, OpDel);
                                }

                                // STORED_OP = 0;
                                // VIEW_ID++;
                                // send_new_view();

                                free(pending);
                                free(pending_buf);

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
