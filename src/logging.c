#include <stdio.h>

void log(char *msg, int type, int log_level)
{
    switch (log_level) 
    {
        case 1:
            fprintf(stderr, msg);
            break;
        case 2:
            if (type)
            {
                fprintf(stderr, msg);
            }
            break;
        default:
            break;
    }
}