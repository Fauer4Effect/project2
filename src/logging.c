#include <stdio.h>
#include <stdarg.h>

#include "logging.h"

void logger(int type, int log_level, int process_id, char *msg, ...)
{
    va_list argptr;
    va_start(argptr, msg);
    switch (log_level) 
    {
        case DEBUG:
            fprintf(stderr, "[%d]\t", process_id);
            vfprintf(stderr, msg, argptr);
            break;
        case INFO:
            if (type)
            {
                fprintf(stderr, "[%d]\t", process_id);
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