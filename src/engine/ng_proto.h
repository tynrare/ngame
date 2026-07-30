// agent: composer-2.5 | 2026-07-25 | binary wire codec | b5e73f
#ifndef NG_PROTO_H
#define NG_PROTO_H

#include "world/ng_world.h"
#include <stddef.h>
#include <stdint.h>

struct NgActionResult;

#define NG_PROTO_MAGIC   0x4E474D45u /* NGME */
// agent: composer-2.5 | 2026-07-29 | lockstep protocol packets | c1fcfa
#define NG_PROTO_VERSION 7

#define NG_CH_UNRELIABLE 0
#define NG_CH_RELIABLE   1

#define NG_PKT_SNAPSHOT   1
#define NG_PKT_INPUT      2
#define NG_PKT_CMD        3
#define NG_PKT_CMD_REPLY  4
// agent: composer-2.5 | 2026-07-25 | remove dead snap ack wire | b4875f
#define NG_PKT_EVENT      5
#define NG_PKT_ACTION_RESULT 6
#define NG_PKT_SESSION       7
#define NG_PKT_STATE_UPDATE  8
#define NG_PKT_STATE_BATCH   9
// agent: composer-2.5 | 2026-07-28 | state ack wire side channel | a6e25f
#define NG_PKT_STATE_ACK    10
#define NG_PKT_REGISTER     11
#define NG_PKT_REGISTER_ACK 12
#define NG_PKT_LOCK_INPUT   13
#define NG_PKT_LOCK_ACK     14
#define NG_PKT_LOCK_HASH    15

#define NG_LOCK_INPUT_MAX 32

typedef struct NgLockInputPkt {
  uint8_t peer_id;
  uint32_t base_tick;
  uint8_t count;
  uint8_t bits[NG_LOCK_INPUT_MAX];
} NgLockInputPkt;

typedef struct NgLockAckPkt {
  uint8_t peer_id;
  uint32_t ack_tick;
} NgLockAckPkt;

typedef struct NgLockHashPkt {
  uint8_t peer_id;
  uint32_t tick;
  uint32_t hash;
} NgLockHashPkt;

typedef enum NgPeerRole {
  NG_PEER_THIN = 0,
  NG_PEER_DEPENDENT = 1,
} NgPeerRole;

typedef struct NgRegisterReq {
  char name[32];
  uint16_t proto_ver;
} NgRegisterReq;

typedef struct NgRegisterAck {
  uint8_t peer_id;
  uint8_t role;
  uint16_t agent_port;
  uint16_t root_game_port;
} NgRegisterAck;

#include "ng_session.h"

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
bool ng_proto_encode_action_result(NgProtoBuf *b, const struct NgActionResult *result);
bool ng_proto_decode_action_result(NgProtoBuf *b, struct NgActionResult *result);
bool ng_proto_encode_session(NgProtoBuf *b, uint16_t seq, const NgSessionState *session);
bool ng_proto_decode_session(NgProtoBuf *b, NgSessionState *session);
bool ng_proto_encode_state_update(NgProtoBuf *b, uint16_t seq, const NgStateUpdate *update);
bool ng_proto_decode_state_update(NgProtoBuf *b, NgStateUpdate *update);
bool ng_proto_encode_state_batch(NgProtoBuf *b, uint16_t seq, uint32_t tick,
                                 const NgStateUpdate *updates, int count);
bool ng_proto_decode_state_batch(NgProtoBuf *b, NgStateUpdate *updates, int max_count, int *out_count);
bool ng_proto_encode_state_ack(NgProtoBuf *b, uint16_t seq, uint32_t entity_id, uint16_t ack_seq);
bool ng_proto_decode_state_ack(NgProtoBuf *b, uint32_t *entity_id, uint16_t *ack_seq);
bool ng_proto_encode_register(NgProtoBuf *b, uint16_t seq, const NgRegisterReq *req);
bool ng_proto_decode_register(NgProtoBuf *b, NgRegisterReq *req);
bool ng_proto_encode_register_ack(NgProtoBuf *b, uint16_t seq, const NgRegisterAck *ack);
bool ng_proto_decode_register_ack(NgProtoBuf *b, NgRegisterAck *ack);
bool ng_proto_encode_lock_input(NgProtoBuf *b, uint16_t seq, const NgLockInputPkt *pkt);
bool ng_proto_decode_lock_input(NgProtoBuf *b, NgLockInputPkt *pkt);
bool ng_proto_encode_lock_ack(NgProtoBuf *b, uint16_t seq, const NgLockAckPkt *pkt);
bool ng_proto_decode_lock_ack(NgProtoBuf *b, NgLockAckPkt *pkt);
bool ng_proto_encode_lock_hash(NgProtoBuf *b, uint16_t seq, const NgLockHashPkt *pkt);
bool ng_proto_decode_lock_hash(NgProtoBuf *b, NgLockHashPkt *pkt);

#endif
// agent: composer-2.5 | 2026-07-29 | lockstep protocol packets | c1fcfa
