
#define DEBUG 0
#define INFO 1
#define SILENT 2

/*
Used to print debug/status messages for debugging, etc.

log_level designates the level of messages to print
    1 = debugging, all messages will print
    2 = info, all messages with type=1 will print messages with type=0 will not print
    0 = silent, no messages will print
*/
void logger(int type, int log_level, char *msg, ...);