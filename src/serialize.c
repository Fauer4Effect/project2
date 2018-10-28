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

    return;
}

void pack_req_message(ReqMessage *msg, unsigned char *buf)
{
    packi32(buf, msg->request_id);
    buf += 4;
    packi32(buf, msg->curr_view_id);
    buf += 4;
    packi32(buf, msg->op_type);
    buf += 4;
    packi32(buf, msg->peer_id);

    return;
}

void unpack_req_message(ReqMessage *msg, unsigned char *buf)
{
    msg->request_id = unpacki32(buf);
    buf += 4;
    msg->curr_view_id = unpacki32(buf);
    buf += 4;
    msg->op_type = unpacki32(buf);
    buf += 4;
    msg->peer_id = unpacki32(buf);

    return;
}

void pack_ok_message(OkMessage *msg, unsigned char *buf)
{
    packi32(buf, msg->request_id);
    buf += 4;
    packi32(buf, msg->curr_view_id);

    return;
}

void unpack_ok_message(OkMessage *msg, unsigned char *buf)
{
    msg->request_id = unpacki32(buf);
    buf += 4;
    msg->curr_view_id = unpacki32(buf);

    return;
}

void pack_view_message(NewViewMessage *view, unsigned char *buf)
{
    packi32(buf, view->view_id);
    buf += 4;
    packi32(buf, view->membership_size);
    buf += 4;

    int index = 0;
    int stored = 0;
    while (stored < view->membership_size)
    {
        if (view->membership_list[index] != 0)
        {
            packi32(buf, view->membership_list[index]);
            // printf("Verify org: %08x packed %08x\n", view->membership_list[index], unpacki32(buf));
            buf += 4;
            stored++;
        }
        index++;
    }

    return;
}

void unpack_view_message(NewViewMessage *view, unsigned char *buf)
{
    view->view_id = unpacki32(buf);
    buf += 4;
    // printf("\tNew View ID: %08x\n", view->view_id);
    view->membership_size = unpacki32(buf);
    buf += 4;
    // printf("\tMembership list size: %08x\n", view->membership_size);
    view->membership_list = malloc(view->membership_size * sizeof(int));

    int index = 0;
    while (index < view->membership_size)
    {
        view->membership_list[index] = unpacki32(buf);
        buf += 4;
        index++;
    }

    return;
}

void pack_heart_beat(HeartBeat *beat, unsigned char *buf)
{
    packi32(buf, beat->process_id);

    return;
}

void unpack_heart_beat(HeartBeat *beat, unsigned char *buf)
{
    beat->process_id = unpacki32(buf);

    return;
}
