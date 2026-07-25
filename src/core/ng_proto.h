// agent: composer-2.5 | 2026-07-25 | binary wire codec | b5e73f
#ifndef NG_PROTO_H
#define NG_PROTO_H

#include "world/ng_world.h"
#include <stddef.h>
#include <stdint.h>

#define NG_PROTO_MAGIC   0x4E474D45u /* NGME */
#define NG_PROTO_VERSION 1

#define NG_CH_UNRELIABLE 0
#define NG_CH_RELIABLE   1

#define NG_PKT_SNAPSHOT   1
#define NG_PKT_INPUT      2
#define NG_PKT_CMD        3
#define NG_PKT_CMD_REPLY  4
// agent: composer-2.5 | 2026-07-25 | remove dead snap ack wire | b4875f
#define NG_PKT_EVENT      5

typedef struct NgProtoHeader {
  uint32_t magic;
  uint8_t version;
  uint8_t channel;
  uint8_t type;
  uint8_t _pad;
  uint16_t seq;
  uint32_t tick;
} NgProtoHeader;

typedef struct NgProtoBuf {
  uint8_t data[65536];
  size_t len;
  size_t pos;
} NgProtoBuf;

void ng_proto_buf_init(NgProtoBuf *b);
bool ng_proto_write_header(NgProtoBuf *b, const NgProtoHeader *h);
bool ng_proto_read_header(NgProtoBuf *b, NgProtoHeader *h);
bool ng_proto_encode_snapshot(NgProtoBuf *b, const NgSnapshot *snap, uint16_t seq,
                              bool delta);
bool ng_proto_decode_snapshot(NgProtoBuf *b, NgSnapshot *snap, bool *delta);
bool ng_proto_encode_input(NgProtoBuf *b, uint16_t seq, uint32_t tick, int buttons,
                           float yaw_delta);
bool ng_proto_decode_input(NgProtoBuf *b, uint16_t *seq, uint32_t *tick, int *buttons,
                           float *yaw_delta);
bool ng_proto_encode_cmd(NgProtoBuf *b, uint16_t seq, const char *line);
bool ng_proto_decode_cmd(NgProtoBuf *b, char *line, size_t line_cap);
bool ng_proto_encode_text(NgProtoBuf *b, uint8_t type, uint16_t seq, const char *text);
bool ng_proto_decode_text(NgProtoBuf *b, char *text, size_t text_cap);

#endif
