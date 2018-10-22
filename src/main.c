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

int main(int argc, char *argv[])
{
    // parse the command line

    // setup all the socket shit

    // send a message to the leader asking to connect

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