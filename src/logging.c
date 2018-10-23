#include <stdio.h>
#include <stdarg.h>

#include "logging.h"

void logger(int type, int log_level, char *msg, ...)
{
    va_list argptr;
    va_start(argptr, msg);
    switch (log_level) 
    {
        case DEBUG:
            vfprintf(stderr, msg, argptr);
            break;
        case INFO:
            if (type)
            {
                vfprintf(stderr, msg, argptr);
            }
            break;
        case SILENT:
            break;
        default:
            break;
    }
    va_end(argptr);
}