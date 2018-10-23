#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

#include "messages.h"

/*
Pack an integer into the buffer
*/
void packi32(unsigned char *buf, uint32_t i)
{
    *buf++ = i >> 24; 
    *buf++ = i >> 16;
    *buf++ = i >> 8;
    *buf++ = i;

    return;
}

/*
Unpack an integer from the buffer
*/
uint32_t unpacki32(unsigned char *buf)
{
    uint32_t i = ((uint32_t) buf[0]<<24) |
                 ((uint32_t) buf[1]<<16) |
                 ((uint32_t) buf[2]<<8)  |
                 buf[3];
    return i;
}

void pack_header(Header *header, unsigned char *buf)
{
    packi32(buf, header->msg_type);
    buf += 4;
    packi32(buf, header->size);

    return;
}

void unpack_header(Header *header, unsigned char *buf)
{
    header->msg_type = unpacki32(buf);
    buf += 4;
    header->size = unpacki32(buf);

    return;
}

void pack_join_message(JoinMessage *msg, unsigned char *buf)
{
    packi32(buf, msg->process_id);

    return;
}

void unpack_join_message(JoinMessage *msg, unsigned char *buf)
{
    msg->process_id = unpacki32(buf);
}