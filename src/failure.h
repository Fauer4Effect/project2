#include <stdint.h>

extern uint32_t *MEMBERSHIP_LIST;
extern char **HOSTS;
extern int NUM_HOSTS;
extern int LOG_LEVEL;
extern int PROCESS_ID;

int bind_failure_detector();