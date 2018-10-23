#define DEBUG 1
#define INFO 2
#define SILENT 0

/*
Used to print debug/status messages for debugging, etc.

log_level designates the level of messages to print
    1 = debugging, all messages will print
    2 = info, all messages with type=1 will print messages with type=0 will not print
    0 = silent, no messages will print
*/
void log(int type, int log_level, char *msg, ...);