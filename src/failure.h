#include <stdint.h>

#include "logging.h"

extern uint32_t *MEMBERSHIP_LIST;
extern char **HOSTS;
extern int NUM_HOSTS;
extern int PROCESS_ID;

typedef struct {
    struct timeval *recvd_time;
} ReceivedHeartBeat;

#define LOG_LEVEL DEBUG

extern ReceivedHeartBeat **RECEIVED_HEARTBEATS;

int bind_failure_detector();

void send_heartbeat(int process_id);

void get_heartbeat(int sockfd);
