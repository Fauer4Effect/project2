#include <stdint.h>

extern uint32_t *MEMBERSHIP_LIST;
extern char **HOSTS;
extern int NUM_HOSTS;
extern int LOG_LEVEL;
extern int PROCESS_ID;
extern ReceivedHeartBeat **RECEIVED_HEARTBEATS;

typedef struct {
    struct timeval *recvd_time;
} ReceivedHeartBeat;

int bind_failure_detector();

void send_heartbeat(int process_id);

int get_heartbeat(int sockfd);