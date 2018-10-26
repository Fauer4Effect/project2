#include <stdint.h>

// #include "messages.h"

void packi32(unsigned char *buf, uint32_t i);

uint32_t unpacki32(unsigned char *buf);

void pack_header(Header *header, unsigned char *buf);

void unpack_header(Header *header, unsigned char *buf);

void pack_join_message(JoinMessage *msg, unsigned char *buf);

void unpack_join_message(JoinMessage *msg, unsigned char *buf);

void pack_req_message(ReqMessage *msg, unsigned char *buf);

void unpack_req_message(ReqMessage *msg, unsigned char *buf);

void pack_ok_message(OkMessage *msg, unsigned char *buf);

void unpack_ok_message(OkMessage *msg, unsigned char *buf);

void pack_view_message(NewViewMessage *view, unsigned char *buf);

void unpack_view_message(NewViewMessage *view, unsigned char *buf);

void pack_heart_beat(HeartBeat *beat, unsigned char *buf);

void unpack_heart_beat(HeartBeat *beat, unsigned char *buf);
