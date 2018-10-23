#include <stdio.h>
#include <stdarg.h>

void log(int type, int log_level, char *msg, ...)
{
    va_list argptr;
    va_start(argptr, msg);
    switch (log_level) 
    {
        case 1:
            vfprintf(stderr, msg, argptr);
            break;
        case 2:
            if (type)
            {
                vfprintf(stderr, msg, argptr);
            }
            break;
        default:
            break;
    }
    va_end(argptr);
}