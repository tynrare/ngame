// agent: composer-2.5 | 2026-07-25 | binary wire codec | b5e73f
// agent: composer-2.5 | 2026-07-25 | input on unreliable channel | f1418f
#include "core/ng_proto.h"
#include "core/ng_action.h"
#include <string.h>

static bool ng_proto_write_u8(NgProtoBuf *b, uint8_t v) {
  if (b->len + 1 > sizeof(b->data)) {
    return false;
  }
  b->data[b->len++] = v;
  return true;
}

static bool ng_proto_write_u16(NgProtoBuf *b, uint16_t v) {
  if (b->len + 2 > sizeof(b->data)) {
    return false;
  }
  b->data[b->len++] = (uint8_t)(v & 0xff);
  b->data[b->len++] = (uint8_t)((v >> 8) & 0xff);
  return true;
}

static bool ng_proto_write_u32(NgProtoBuf *b, uint32_t v) {
  if (b->len + 4 > sizeof(b->data)) {
    return false;
  }
  b->data[b->len++] = (uint8_t)(v & 0xff);
  b->data[b->len++] = (uint8_t)((v >> 8) & 0xff);
  b->data[b->len++] = (uint8_t)((v >> 16) & 0xff);
  b->data[b->len++] = (uint8_t)((v >> 24) & 0xff);
  return true;
}

static bool ng_proto_write_f32(NgProtoBuf *b, float v) {
  uint32_t u;
  memcpy(&u, &v, sizeof(u));
  return ng_proto_write_u32(b, u);
}

static bool ng_proto_read_u8(NgProtoBuf *b, uint8_t *v) {
  if (b->pos + 1 > b->len) {
    return false;
  }
  *v = b->data[b->pos++];
  return true;
}

static bool ng_proto_read_u16(NgProtoBuf *b, uint16_t *v) {
  if (b->pos + 2 > b->len) {
    return false;
  }
  *v = (uint16_t)b->data[b->pos] | ((uint16_t)b->data[b->pos + 1] << 8);
  b->pos += 2;
  return true;
}

static bool ng_proto_read_u32(NgProtoBuf *b, uint32_t *v) {
  if (b->pos + 4 > b->len) {
    return false;
  }
  *v = (uint32_t)b->data[b->pos] | ((uint32_t)b->data[b->pos + 1] << 8) |
       ((uint32_t)b->data[b->pos + 2] << 16) | ((uint32_t)b->data[b->pos + 3] << 24);
  b->pos += 4;
  return true;
}

static bool ng_proto_read_f32(NgProtoBuf *b, float *v) {
  uint32_t u;
  if (!ng_proto_read_u32(b, &u)) {
    return false;
  }
  memcpy(v, &u, sizeof(*v));
  return true;
}

void ng_proto_buf_init(NgProtoBuf *b) {
  b->len = 0;
  b->pos = 0;
}

bool ng_proto_write_header(NgProtoBuf *b, const NgProtoHeader *h) {
  return ng_proto_write_u32(b, h->magic) && ng_proto_write_u8(b, h->version) &&
         ng_proto_write_u8(b, h->channel) && ng_proto_write_u8(b, h->type) &&
         ng_proto_write_u8(b, h->_pad) && ng_proto_write_u16(b, h->seq) &&
         ng_proto_write_u32(b, h->tick);
}

bool ng_proto_read_header(NgProtoBuf *b, NgProtoHeader *h) {
  return ng_proto_read_u32(b, &h->magic) && ng_proto_read_u8(b, &h->version) &&
         ng_proto_read_u8(b, &h->channel) && ng_proto_read_u8(b, &h->type) &&
         ng_proto_read_u8(b, &h->_pad) && ng_proto_read_u16(b, &h->seq) &&
         ng_proto_read_u32(b, &h->tick);
}

bool ng_proto_encode_snapshot(NgProtoBuf *b, const NgSnapshot *snap, uint16_t seq,
                              bool delta) {
  ng_proto_buf_init(b);
  NgProtoHeader h = {
      .magic = NG_PROTO_MAGIC,
      .version = NG_PROTO_VERSION,
      .channel = NG_CH_UNRELIABLE,
      .type = NG_PKT_SNAPSHOT,
      .seq = seq,
      .tick = snap->tick,
  };
  if (!ng_proto_write_header(b, &h) || !ng_proto_write_u8(b, delta ? 1 : 0)) {
    return false;
  }
  const size_t scene_len = strnlen(snap->scene_id, 31);
  if (!ng_proto_write_u8(b, (uint8_t)scene_len)) {
    return false;
  }
  for (size_t i = 0; i < scene_len; i++) {
    if (!ng_proto_write_u8(b, (uint8_t)snap->scene_id[i])) {
      return false;
    }
  }
  if (!ng_proto_write_u16(b, (uint16_t)snap->entity_count)) {
    return false;
  }
  for (int i = 0; i < snap->entity_count; i++) {
    const NgEntitySnap *e = &snap->entities[i];
    if (!ng_proto_write_u32(b, e->id) || !ng_proto_write_u8(b, e->type) ||
        !ng_proto_write_u32(b, e->comp_mask)) {
      return false;
    }
    if (e->comp_mask & NG_COMP_POS) {
      if (!ng_proto_write_f32(b, e->pos[0]) || !ng_proto_write_f32(b, e->pos[1]) ||
          !ng_proto_write_f32(b, e->pos[2])) {
        return false;
      }
    }
    if (e->comp_mask & NG_COMP_ROT) {
      if (!ng_proto_write_f32(b, e->rot_y)) {
        return false;
      }
    }
    if (e->comp_mask & NG_COMP_PHASE) {
      if (!ng_proto_write_f32(b, e->phase)) {
        return false;
      }
    }
    if (e->comp_mask & NG_COMP_FLAGS) {
      if (!ng_proto_write_u32(b, e->flags)) {
        return false;
      }
    }
  }
  return true;
}

static bool ng_proto_apply_entity(NgSnapshot *snap, const NgEntitySnap *e) {
  for (int i = 0; i < snap->entity_count; i++) {
    if (snap->entities[i].id == e->id) {
      NgEntitySnap *dst = &snap->entities[i];
      if (e->comp_mask & NG_COMP_TYPE) {
        dst->type = e->type;
      }
      if (e->comp_mask & NG_COMP_POS) {
        dst->pos[0] = e->pos[0];
        dst->pos[1] = e->pos[1];
        dst->pos[2] = e->pos[2];
      }
      if (e->comp_mask & NG_COMP_ROT) {
        dst->rot_y = e->rot_y;
      }
      if (e->comp_mask & NG_COMP_PHASE) {
        dst->phase = e->phase;
      }
      if (e->comp_mask & NG_COMP_FLAGS) {
        dst->flags = e->flags;
      }
      return true;
    }
  }
  if (snap->entity_count >= NG_SNAPSHOT_ENTITY_MAX) {
    return false;
  }
  snap->entities[snap->entity_count++] = *e;
  return true;
}

bool ng_proto_decode_snapshot(NgProtoBuf *b, NgSnapshot *snap, bool *delta) {
  uint8_t d = 0;
  uint8_t scene_len = 0;
  uint16_t count = 0;
  if (!ng_proto_read_u8(b, &d) || !ng_proto_read_u8(b, &scene_len)) {
    return false;
  }
  *delta = (d != 0);
  if (scene_len >= sizeof(snap->scene_id)) {
    return false;
  }
  for (int i = 0; i < scene_len; i++) {
    uint8_t c;
    if (!ng_proto_read_u8(b, &c)) {
      return false;
    }
    snap->scene_id[i] = (char)c;
  }
  snap->scene_id[scene_len] = '\0';
  if (!ng_proto_read_u16(b, &count)) {
    return false;
  }
  if (!*delta) {
    snap->entity_count = 0;
  }
  for (int i = 0; i < count; i++) {
    NgEntitySnap e = {0};
    if (!ng_proto_read_u32(b, &e.id) || !ng_proto_read_u8(b, &e.type) ||
        !ng_proto_read_u32(b, &e.comp_mask)) {
      return false;
    }
    if (e.comp_mask & NG_COMP_POS) {
      if (!ng_proto_read_f32(b, &e.pos[0]) || !ng_proto_read_f32(b, &e.pos[1]) ||
          !ng_proto_read_f32(b, &e.pos[2])) {
        return false;
      }
    }
    if (e.comp_mask & NG_COMP_ROT) {
      if (!ng_proto_read_f32(b, &e.rot_y)) {
        return false;
      }
    }
    if (e.comp_mask & NG_COMP_PHASE) {
      if (!ng_proto_read_f32(b, &e.phase)) {
        return false;
      }
    }
    if (e.comp_mask & NG_COMP_FLAGS) {
      if (!ng_proto_read_u32(b, &e.flags)) {
        return false;
      }
    }
    if (*delta) {
      if (!ng_proto_apply_entity(snap, &e)) {
        return false;
      }
    } else {
      if (snap->entity_count >= NG_SNAPSHOT_ENTITY_MAX) {
        return false;
      }
      snap->entities[snap->entity_count++] = e;
    }
  }
  return true;
}

bool ng_proto_encode_input(NgProtoBuf *b, uint16_t seq, uint32_t tick, int buttons,
                           float yaw_delta) {
  ng_proto_buf_init(b);
  NgProtoHeader h = {
      .magic = NG_PROTO_MAGIC,
      .version = NG_PROTO_VERSION,
      .channel = NG_CH_UNRELIABLE,
      .type = NG_PKT_INPUT,
      .seq = seq,
      .tick = tick,
  };
  return ng_proto_write_header(b, &h) && ng_proto_write_u32(b, (uint32_t)buttons) &&
         ng_proto_write_f32(b, yaw_delta);
}

bool ng_proto_decode_input(NgProtoBuf *b, uint16_t *seq, uint32_t *tick, int *buttons,
                           float *yaw_delta) {
  NgProtoHeader h;
  b->pos = 0;
  if (!ng_proto_read_header(b, &h)) {
    return false;
  }
  *seq = h.seq;
  *tick = h.tick;
  uint32_t btn = 0;
  if (!ng_proto_read_u32(b, &btn) || !ng_proto_read_f32(b, yaw_delta)) {
    return false;
  }
  *buttons = (int)btn;
  return true;
}

bool ng_proto_encode_cmd(NgProtoBuf *b, uint16_t seq, const char *line) {
  ng_proto_buf_init(b);
  NgProtoHeader h = {
      .magic = NG_PROTO_MAGIC,
      .version = NG_PROTO_VERSION,
      .channel = NG_CH_RELIABLE,
      .type = NG_PKT_CMD,
      .seq = seq,
      .tick = 0,
  };
  const size_t n = strnlen(line, 255);
  return ng_proto_write_header(b, &h) && ng_proto_write_u8(b, (uint8_t)n) &&
         (n == 0 || memcpy(b->data + b->len, line, n), b->len += n, true);
}

bool ng_proto_decode_cmd(NgProtoBuf *b, char *line, size_t line_cap) {
  uint8_t n = 0;
  if (!ng_proto_read_u8(b, &n) || n >= line_cap) {
    return false;
  }
  for (int i = 0; i < n; i++) {
    uint8_t c;
    if (!ng_proto_read_u8(b, &c)) {
      return false;
    }
    line[i] = (char)c;
  }
  line[n] = '\0';
  return true;
}

// agent: composer-2.5 | 2026-07-25 | snapshot payload sans header | a8b9c0
static bool ng_proto_write_snapshot_payload(NgProtoBuf *b, const NgSnapshot *snap) {
  if (!ng_proto_write_u8(b, 0)) {
    return false;
  }
  const size_t scene_len = strnlen(snap->scene_id, 31);
  if (!ng_proto_write_u8(b, (uint8_t)scene_len)) {
    return false;
  }
  for (size_t i = 0; i < scene_len; i++) {
    if (!ng_proto_write_u8(b, (uint8_t)snap->scene_id[i])) {
      return false;
    }
  }
  if (!ng_proto_write_u16(b, (uint16_t)snap->entity_count)) {
    return false;
  }
  for (int i = 0; i < snap->entity_count; i++) {
    const NgEntitySnap *e = &snap->entities[i];
    if (!ng_proto_write_u32(b, e->id) || !ng_proto_write_u8(b, e->type) ||
        !ng_proto_write_u32(b, e->comp_mask)) {
      return false;
    }
    if (e->comp_mask & NG_COMP_POS) {
      if (!ng_proto_write_f32(b, e->pos[0]) || !ng_proto_write_f32(b, e->pos[1]) ||
          !ng_proto_write_f32(b, e->pos[2])) {
        return false;
      }
    }
    if (e->comp_mask & NG_COMP_ROT) {
      if (!ng_proto_write_f32(b, e->rot_y)) {
        return false;
      }
    }
    if (e->comp_mask & NG_COMP_PHASE) {
      if (!ng_proto_write_f32(b, e->phase)) {
        return false;
      }
    }
    if (e->comp_mask & NG_COMP_FLAGS) {
      if (!ng_proto_write_u32(b, e->flags)) {
        return false;
      }
    }
  }
  return true;
}

static bool ng_proto_read_snapshot_payload(NgProtoBuf *b, NgSnapshot *snap) {
  bool delta = false;
  return ng_proto_decode_snapshot(b, snap, &delta);
}

bool ng_proto_encode_action_result(NgProtoBuf *b, const NgActionResult *result) {
  if (!b || !result) {
    return false;
  }
  ng_proto_buf_init(b);
  NgProtoHeader h = {
      .magic = NG_PROTO_MAGIC,
      .version = NG_PROTO_VERSION,
      .channel = NG_CH_RELIABLE,
      .type = NG_PKT_ACTION_RESULT,
      .seq = result->action_seq,
      .tick = result->server_tick,
  };
  const size_t reply_len = strnlen(result->reply, sizeof(result->reply) - 1);
  if (!ng_proto_write_header(b, &h) || !ng_proto_write_u32(b, result->state_hash) ||
      !ng_proto_write_u8(b, (uint8_t)result->kind) ||
      !ng_proto_write_u16(b, (uint16_t)reply_len)) {
    return false;
  }
  for (size_t i = 0; i < reply_len; i++) {
    if (!ng_proto_write_u8(b, (uint8_t)result->reply[i])) {
      return false;
    }
  }
  if (!ng_proto_write_u8(b, result->have_state ? 1 : 0)) {
    return false;
  }
  if (result->have_state && !ng_proto_write_snapshot_payload(b, &result->state)) {
    return false;
  }
  return true;
}

bool ng_proto_decode_action_result(NgProtoBuf *b, NgActionResult *result) {
  if (!b || !result) {
    return false;
  }
  NgProtoHeader h;
  b->pos = 0;
  if (!ng_proto_read_header(b, &h)) {
    return false;
  }
  result->action_seq = h.seq;
  result->server_tick = h.tick;
  uint32_t hash = 0;
  uint8_t kind = 0;
  uint16_t reply_len = 0;
  if (!ng_proto_read_u32(b, &hash) || !ng_proto_read_u8(b, &kind) ||
      !ng_proto_read_u16(b, &reply_len)) {
    return false;
  }
  result->state_hash = hash;
  result->kind = (NgActionKind)kind;
  if (reply_len >= sizeof(result->reply)) {
    return false;
  }
  for (int i = 0; i < reply_len; i++) {
    uint8_t c;
    if (!ng_proto_read_u8(b, &c)) {
      return false;
    }
    result->reply[i] = (char)c;
  }
  result->reply[reply_len] = '\0';
  uint8_t have_state = 0;
  if (!ng_proto_read_u8(b, &have_state)) {
    return false;
  }
  result->have_state = (have_state != 0);
  if (result->have_state) {
    memset(&result->state, 0, sizeof(result->state));
    if (!ng_proto_read_snapshot_payload(b, &result->state)) {
      return false;
    }
  }
  return true;
}

bool ng_proto_encode_text(NgProtoBuf *b, uint8_t type, uint16_t seq, const char *text) {
  ng_proto_buf_init(b);
  NgProtoHeader h = {
      .magic = NG_PROTO_MAGIC,
      .version = NG_PROTO_VERSION,
      .channel = NG_CH_RELIABLE,
      .type = type,
      .seq = seq,
      .tick = 0,
  };
  const size_t n = strnlen(text, 1023);
  return ng_proto_write_header(b, &h) && ng_proto_write_u16(b, (uint16_t)n) &&
         (n == 0 || memcpy(b->data + b->len, text, n), b->len += n, true);
}

bool ng_proto_decode_text(NgProtoBuf *b, char *text, size_t text_cap) {
  uint16_t n = 0;
  if (!ng_proto_read_u16(b, &n) || n >= text_cap) {
    return false;
  }
  for (int i = 0; i < n; i++) {
    uint8_t c;
    if (!ng_proto_read_u8(b, &c)) {
      return false;
    }
    text[i] = (char)c;
  }
  text[n] = '\0';
  return true;
}

// agent: composer-2.5 | 2026-07-26 | session state update wire | e7f8a9
bool ng_proto_encode_session(NgProtoBuf *b, uint16_t seq, const NgSessionState *session) {
  if (!b || !session) {
    return false;
  }
  ng_proto_buf_init(b);
  NgProtoHeader h = {
      .magic = NG_PROTO_MAGIC,
      .version = NG_PROTO_VERSION,
      .channel = NG_CH_RELIABLE,
      .type = NG_PKT_SESSION,
      .seq = seq,
      .tick = session->tick,
  };
  const size_t scene_len = strnlen(session->scene_id, sizeof(session->scene_id) - 1);
  return ng_proto_write_header(b, &h) && ng_proto_write_u8(b, (uint8_t)scene_len) &&
         (scene_len == 0 ||
          memcpy(b->data + b->len, session->scene_id, scene_len), b->len += scene_len, true) &&
         ng_proto_write_u8(b, session->controller_id) && ng_proto_write_u8(b, session->your_id) &&
         ng_proto_write_u32(b, session->cube_entity_id) &&
         ng_proto_write_u8(b, session->client_fields ? 1 : 0);
}

bool ng_proto_decode_session(NgProtoBuf *b, NgSessionState *session) {
  if (!b || !session) {
    return false;
  }
  memset(session, 0, sizeof(*session));
  uint8_t scene_len = 0;
  if (!ng_proto_read_u8(b, &scene_len) || scene_len >= sizeof(session->scene_id)) {
    return false;
  }
  for (int i = 0; i < scene_len; i++) {
    uint8_t c;
    if (!ng_proto_read_u8(b, &c)) {
      return false;
    }
    session->scene_id[i] = (char)c;
  }
  session->scene_id[scene_len] = '\0';
  uint8_t cf = 0;
  return ng_proto_read_u8(b, &session->controller_id) && ng_proto_read_u8(b, &session->your_id) &&
         ng_proto_read_u32(b, &session->cube_entity_id) && ng_proto_read_u8(b, &cf) &&
         (session->client_fields = (cf != 0), true);
}

bool ng_proto_encode_state_update(NgProtoBuf *b, uint16_t seq, const NgStateUpdate *update) {
  if (!b || !update) {
    return false;
  }
  ng_proto_buf_init(b);
  NgProtoHeader h = {
      .magic = NG_PROTO_MAGIC,
      .version = NG_PROTO_VERSION,
      .channel = NG_CH_UNRELIABLE,
      .type = NG_PKT_STATE_UPDATE,
      .seq = seq,
      .tick = update->tick,
  };
  return ng_proto_write_header(b, &h) && ng_proto_write_u32(b, update->entity_id) &&
         ng_proto_write_f32(b, update->rot_y);
}

bool ng_proto_decode_state_update(NgProtoBuf *b, NgStateUpdate *update) {
  if (!b || !update) {
    return false;
  }
  return ng_proto_read_u32(b, &update->entity_id) && ng_proto_read_f32(b, &update->rot_y);
}
