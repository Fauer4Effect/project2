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

#define LOG_LEVEL 1                             // set log level to debug for logging
#define max(A, B) ((A) > (B) ? (A) : (B))

int PORT;
int PROCESS_ID;

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[])
{
    // parse the command line
    if (argc < 4)
    {
        log(1, LOG_LEVEL, "USAGE:\n prj2 -p port -h hostfile");
    }

    // setup all the socket shit
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
        log(1, LOG_LEVEL, "listen");
        exit(1);
    }

    // add the listener to the master set
    FD_SET(listener, &master);
    // track the biggest file descriptor
    fdmax = listener;

    // if leader
        
        // initialize membership list to include self
        // because we all know all of the members at the start
        // ??? could this just be an int[] that just references hosts array

    // else
        
        // send a message to the leader asking to join

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