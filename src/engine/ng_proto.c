// agent: composer-2.5 | 2026-07-25 | binary wire codec | b5e73f
// agent: composer-2.5 | 2026-07-25 | input on unreliable channel | f1418f
#include "engine/ng_proto.h"
#include "engine/ng_action.h"
#include <math.h>
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
  if (!ng_proto_write_header(b, &h) || !ng_proto_write_u8(b, (uint8_t)scene_len)) {
    return false;
  }
  if (scene_len > 0) {
    memcpy(b->data + b->len, session->scene_id, scene_len);
    b->len += scene_len;
  }
  // agent: composer-2.5 | 2026-07-29 | lockstep protocol packets | 30ad80
  // agent: composer-2.5 | 2026-07-30 | proto v8 lock phys packets | 259e88
  // agent: composer-2.5 | 2026-08-01 | session playout encode | 5be998
  // agent: composer-2.5 | 2026-08-01 | session mode wire byte | de3bcd
  if (!ng_proto_write_u8(b, session->controller_id) || !ng_proto_write_u8(b, session->your_id) ||
      !ng_proto_write_u8(b, (uint8_t)session->scene_sync) ||
      !ng_proto_write_u8(b, session->lockstep) ||
      !ng_proto_write_u8(b, session->syncing ? 1u : 0u) ||
      !ng_proto_write_u32(b, session->snap_tick) || !ng_proto_write_u8(b, session->playout) ||
      !ng_proto_write_u8(b, (uint8_t)session->spawn_count) ||
      session->spawn_count > NG_SESSION_SPAWN_MAX) {
    return false;
  }
  // agent: composer-2.5 | 2026-07-29 | proto v6 session encode decode | a782bd
  for (int i = 0; i < session->spawn_count; i++) {
    const NgSessionSpawn *sp = &session->spawns[i];
    const size_t name_len = strnlen(sp->desc_name, sizeof(sp->desc_name) - 1);
    const size_t key_len = strnlen(sp->key, sizeof(sp->key) - 1);
    if (!ng_proto_write_u32(b, sp->entity_id) || !ng_proto_write_u8(b, (uint8_t)name_len)) {
      return false;
    }
    for (size_t j = 0; j < name_len; j++) {
      if (!ng_proto_write_u8(b, (uint8_t)sp->desc_name[j])) {
        return false;
      }
    }
    if (!ng_proto_write_u8(b, (uint8_t)key_len)) {
      return false;
    }
    for (size_t j = 0; j < key_len; j++) {
      if (!ng_proto_write_u8(b, (uint8_t)sp->key[j])) {
        return false;
      }
    }
    if (!ng_proto_write_u8(b, (uint8_t)sp->sync) || !ng_proto_write_f32(b, sp->pos[0]) ||
        !ng_proto_write_f32(b, sp->pos[1]) || !ng_proto_write_f32(b, sp->pos[2]) ||
        !ng_proto_write_f32(b, sp->rot[0]) || !ng_proto_write_f32(b, sp->rot[1]) ||
        !ng_proto_write_f32(b, sp->rot[2]) || !ng_proto_write_f32(b, sp->scale)) {
      return false;
    }
  }
  return true;
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
  uint8_t sync = 0;
  uint8_t lockstep = 0;
  if (!ng_proto_read_u8(b, &session->controller_id) || !ng_proto_read_u8(b, &session->your_id) ||
      !ng_proto_read_u8(b, &sync)) {
    return false;
  }
  session->scene_sync = (NgSyncMode)sync;
  if (b->pos >= b->len) {
    return true;
  }
  // agent: composer-2.5 | 2026-07-29 | lockstep protocol packets | 30ad80
  // agent: composer-2.5 | 2026-07-30 | proto v8 lock phys packets | 259e88
  if (!ng_proto_read_u8(b, &lockstep)) {
    return false;
  }
  /* 0=off, 1=pure lockstep, 2=hybrid (proto v11). Clamp unknown → off. */
  // agent: composer-2.5 | 2026-08-01 | session mode wire byte | de3bcd
  session->lockstep = (lockstep <= 2u) ? lockstep : 0u;
  if (b->pos >= b->len) {
    return true;
  }
  uint8_t syncing = 0;
  if (!ng_proto_read_u8(b, &syncing)) {
    return false;
  }
  session->syncing = syncing ? 1u : 0u;
  if (b->pos + 4 > b->len) {
    return true;
  }
  if (!ng_proto_read_u32(b, &session->snap_tick)) {
    return false;
  }
  if (b->pos >= b->len) {
    return true;
  }
  // agent: composer-2.5 | 2026-08-01 | session playout encode | 5be998
  {
    uint8_t playout = 0;
    if (!ng_proto_read_u8(b, &playout)) {
      return false;
    }
    session->playout = playout;
  }
  if (b->pos >= b->len) {
    return true;
  }
  uint8_t spawn_count = 0;
  if (!ng_proto_read_u8(b, &spawn_count) || spawn_count > NG_SESSION_SPAWN_MAX) {
    return false;
  }
  // agent: composer-2.5 | 2026-07-29 | proto v6 session encode decode | a782bd
  session->spawn_count = spawn_count;
  for (int i = 0; i < spawn_count; i++) {
    NgSessionSpawn *sp = &session->spawns[i];
    uint8_t name_len = 0;
    uint8_t key_len = 0;
    if (!ng_proto_read_u32(b, &sp->entity_id) || !ng_proto_read_u8(b, &name_len) ||
        name_len >= sizeof(sp->desc_name)) {
      return false;
    }
    for (int j = 0; j < name_len; j++) {
      uint8_t c;
      if (!ng_proto_read_u8(b, &c)) {
        return false;
      }
      sp->desc_name[j] = (char)c;
    }
    sp->desc_name[name_len] = '\0';
    if (!ng_proto_read_u8(b, &key_len) || key_len >= sizeof(sp->key)) {
      return false;
    }
    for (int j = 0; j < key_len; j++) {
      uint8_t c;
      if (!ng_proto_read_u8(b, &c)) {
        return false;
      }
      sp->key[j] = (char)c;
    }
    sp->key[key_len] = '\0';
    uint8_t sp_sync = 0;
    if (!ng_proto_read_u8(b, &sp_sync) || !ng_proto_read_f32(b, &sp->pos[0]) ||
        !ng_proto_read_f32(b, &sp->pos[1]) || !ng_proto_read_f32(b, &sp->pos[2]) ||
        !ng_proto_read_f32(b, &sp->rot[0]) || !ng_proto_read_f32(b, &sp->rot[1]) ||
        !ng_proto_read_f32(b, &sp->rot[2]) || !ng_proto_read_f32(b, &sp->scale)) {
      return false;
    }
    sp->sync = (NgSyncMode)sp_sync;
  }
  return true;
}

static int16_t ng_proto_quant_cm(float v) {
  float q = roundf(v * 100.0f);
  if (q > 32767.0f) {
    q = 32767.0f;
  }
  if (q < -32768.0f) {
    q = -32768.0f;
  }
  return (int16_t)q;
}

static float ng_proto_dequant_cm(int16_t v) { return (float)v / 100.0f; }

// agent: composer-2.5 | 2026-07-30 | proto quantize lin ang vel | b9fdec
/* Linear: cm/s (same scale as position). Angular: mrad/s. */
static int16_t ng_proto_quant_vel(float v) { return ng_proto_quant_cm(v); }
static float ng_proto_dequant_vel(int16_t v) { return ng_proto_dequant_cm(v); }
static int16_t ng_proto_quant_ang_vel(float rad_s) {
  float q = rad_s * 1000.0f;
  if (q > 32767.0f) {
    q = 32767.0f;
  }
  if (q < -32768.0f) {
    q = -32768.0f;
  }
  return (int16_t)q;
}
static float ng_proto_dequant_ang_vel(int16_t v) { return (float)v / 1000.0f; }

// agent: composer-2.5 | 2026-07-29 | full-circle angle wire quant | a7e2c4
static uint16_t ng_proto_quant_angle(float rad) {
  const float tau = 6.28318530718f;
  float w = fmodf(rad, tau);
  if (w < 0.0f) {
    w += tau;
  }
  return (uint16_t)(w / tau * 65535.0f + 0.5f);
}

static float ng_proto_dequant_angle(uint16_t v) {
  return (float)v / 65535.0f * 6.28318530718f;
}

// agent: composer-2.5 | 2026-08-01 | smallest three quat wire | b4fc42
static void ng_proto_euler_xyz_to_quat(const float euler[3], float q[4]) {
  const float cx = cosf(euler[0] * 0.5f);
  const float sx = sinf(euler[0] * 0.5f);
  const float cy = cosf(euler[1] * 0.5f);
  const float sy = sinf(euler[1] * 0.5f);
  const float cz = cosf(euler[2] * 0.5f);
  const float sz = sinf(euler[2] * 0.5f);
  q[0] = sx * cy * cz + cx * sy * sz;
  q[1] = cx * sy * cz - sx * cy * sz;
  q[2] = cx * cy * sz + sx * sy * cz;
  q[3] = cx * cy * cz - sx * sy * sz;
}

static void ng_proto_quat_to_euler_xyz(const float q[4], float euler[3]) {
  const float sinr_cosp = 2.0f * (q[3] * q[0] + q[1] * q[2]);
  const float cosr_cosp = 1.0f - 2.0f * (q[0] * q[0] + q[1] * q[1]);
  euler[0] = atan2f(sinr_cosp, cosr_cosp);
  const float sinp = 2.0f * (q[3] * q[1] - q[2] * q[0]);
  if (fabsf(sinp) >= 1.0f) {
    euler[1] = copysignf(1.57079632679f, sinp);
  } else {
    euler[1] = asinf(sinp);
  }
  const float siny_cosp = 2.0f * (q[3] * q[2] + q[0] * q[1]);
  const float cosy_cosp = 1.0f - 2.0f * (q[1] * q[1] + q[2] * q[2]);
  euler[2] = atan2f(siny_cosp, cosy_cosp);
}

static uint32_t ng_proto_pack_quat_smallest_three(const float euler[3]) {
  float q[4];
  ng_proto_euler_xyz_to_quat(euler, q);
  float len2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
  if (len2 > 1e-8f) {
    const float inv = 1.0f / sqrtf(len2);
    q[0] *= inv;
    q[1] *= inv;
    q[2] *= inv;
    q[3] *= inv;
  }
  int largest = 0;
  float largest_val = fabsf(q[0]);
  for (int i = 1; i < 4; i++) {
    const float v = fabsf(q[i]);
    if (v > largest_val) {
      largest_val = v;
      largest = i;
    }
  }
  if (q[largest] < 0.0f) {
    q[0] = -q[0];
    q[1] = -q[1];
    q[2] = -q[2];
    q[3] = -q[3];
  }
  uint32_t pack = (uint32_t)largest;
  int slot = 0;
  for (int i = 0; i < 4; i++) {
    if (i == largest) {
      continue;
    }
    float v = q[i];
    if (v < -0.707107f) {
      v = -0.707107f;
    }
    if (v > 0.707107f) {
      v = 0.707107f;
    }
    uint32_t enc = (uint32_t)((v + 0.707107f) / 1.414214f * 1023.0f + 0.5f);
    if (enc > 1023u) {
      enc = 1023u;
    }
    pack |= enc << (2 + slot * 10);
    slot++;
  }
  return pack;
}

static void ng_proto_unpack_quat_smallest_three(uint32_t pack, float euler[3]) {
  const int largest = (int)(pack & 3u);
  float q[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  int slot = 0;
  for (int i = 0; i < 4; i++) {
    if (i == largest) {
      continue;
    }
    const uint32_t enc = (pack >> (2 + slot * 10)) & 1023u;
    q[i] = (float)enc / 1023.0f * 1.414214f - 0.707107f;
    slot++;
  }
  float sum = 0.0f;
  for (int i = 0; i < 4; i++) {
    if (i != largest) {
      sum += q[i] * q[i];
    }
  }
  q[largest] = sqrtf(sum > 1.0f ? 0.0f : 1.0f - sum);
  ng_proto_quat_to_euler_xyz(q, euler);
}

static bool ng_proto_write_i16(NgProtoBuf *b, int16_t v) {
  return ng_proto_write_u16(b, (uint16_t)v);
}

static bool ng_proto_read_i16(NgProtoBuf *b, int16_t *out) {
  uint16_t u = 0;
  if (!ng_proto_read_u16(b, &u)) {
    return false;
  }
  *out = (int16_t)u;
  return true;
}

static bool ng_proto_write_state_body(NgProtoBuf *b, const NgStateUpdate *update) {
  // agent: composer-2.5 | 2026-07-30 | proto quantize lin ang vel | b9fdec
  return ng_proto_write_u32(b, update->entity_id) && ng_proto_write_u16(b, update->seq) &&
         ng_proto_write_u8(b, update->comp_mask) &&
         (!(update->comp_mask & NG_COMP_POS) ||
          (ng_proto_write_i16(b, ng_proto_quant_cm(update->pos[0])) &&
           ng_proto_write_i16(b, ng_proto_quant_cm(update->pos[1])) &&
           ng_proto_write_i16(b, ng_proto_quant_cm(update->pos[2])))) &&
         (!(update->comp_mask & NG_COMP_ROT) ||
          ng_proto_write_u32(b, ng_proto_pack_quat_smallest_three(update->rot))) &&
         (!(update->comp_mask & NG_COMP_SCALE) ||
          ng_proto_write_i16(b, ng_proto_quant_cm(update->scale))) &&
         (!(update->comp_mask & NG_COMP_LIN_VEL) ||
          (ng_proto_write_i16(b, ng_proto_quant_vel(update->lin_vel[0])) &&
           ng_proto_write_i16(b, ng_proto_quant_vel(update->lin_vel[1])) &&
           ng_proto_write_i16(b, ng_proto_quant_vel(update->lin_vel[2])))) &&
         (!(update->comp_mask & NG_COMP_ANG_VEL) ||
          (ng_proto_write_i16(b, ng_proto_quant_ang_vel(update->ang_vel[0])) &&
           ng_proto_write_i16(b, ng_proto_quant_ang_vel(update->ang_vel[1])) &&
           ng_proto_write_i16(b, ng_proto_quant_ang_vel(update->ang_vel[2]))));
}

static bool ng_proto_read_state_body(NgProtoBuf *b, NgStateUpdate *update) {
  // agent: composer-2.5 | 2026-07-30 | proto quantize lin ang vel | b9fdec
  uint8_t mask = 0;
  if (!ng_proto_read_u32(b, &update->entity_id) || !ng_proto_read_u16(b, &update->seq) ||
      !ng_proto_read_u8(b, &mask)) {
    return false;
  }
  update->comp_mask = mask;
  if (mask & NG_COMP_POS) {
    int16_t q0 = 0, q1 = 0, q2 = 0;
    if (!ng_proto_read_i16(b, &q0) || !ng_proto_read_i16(b, &q1) || !ng_proto_read_i16(b, &q2)) {
      return false;
    }
    update->pos[0] = ng_proto_dequant_cm(q0);
    update->pos[1] = ng_proto_dequant_cm(q1);
    update->pos[2] = ng_proto_dequant_cm(q2);
  }
  if (mask & NG_COMP_ROT) {
    uint32_t packed = 0;
    if (!ng_proto_read_u32(b, &packed)) {
      return false;
    }
    ng_proto_unpack_quat_smallest_three(packed, update->rot);
  }
  if (mask & NG_COMP_SCALE) {
    int16_t qs = 0;
    if (!ng_proto_read_i16(b, &qs)) {
      return false;
    }
    update->scale = ng_proto_dequant_cm(qs);
  }
  if (mask & NG_COMP_LIN_VEL) {
    int16_t q0 = 0, q1 = 0, q2 = 0;
    if (!ng_proto_read_i16(b, &q0) || !ng_proto_read_i16(b, &q1) || !ng_proto_read_i16(b, &q2)) {
      return false;
    }
    update->lin_vel[0] = ng_proto_dequant_vel(q0);
    update->lin_vel[1] = ng_proto_dequant_vel(q1);
    update->lin_vel[2] = ng_proto_dequant_vel(q2);
  }
  if (mask & NG_COMP_ANG_VEL) {
    int16_t q0 = 0, q1 = 0, q2 = 0;
    if (!ng_proto_read_i16(b, &q0) || !ng_proto_read_i16(b, &q1) || !ng_proto_read_i16(b, &q2)) {
      return false;
    }
    update->ang_vel[0] = ng_proto_dequant_ang_vel(q0);
    update->ang_vel[1] = ng_proto_dequant_ang_vel(q1);
    update->ang_vel[2] = ng_proto_dequant_ang_vel(q2);
  }
  return true;
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
  return ng_proto_write_header(b, &h) && ng_proto_write_state_body(b, update);
}

bool ng_proto_decode_state_update(NgProtoBuf *b, NgStateUpdate *update) {
  if (!b || !update) {
    return false;
  }
  return ng_proto_read_state_body(b, update);
}

bool ng_proto_encode_state_batch(NgProtoBuf *b, uint16_t seq, uint32_t tick,
                                 const NgStateUpdate *updates, int count) {
  if (!b || !updates || count <= 0 || count > 255) {
    return false;
  }
  ng_proto_buf_init(b);
  NgProtoHeader h = {
      .magic = NG_PROTO_MAGIC,
      .version = NG_PROTO_VERSION,
      .channel = NG_CH_UNRELIABLE,
      .type = NG_PKT_STATE_BATCH,
      .seq = seq,
      .tick = tick,
  };
  if (!ng_proto_write_header(b, &h) || !ng_proto_write_u8(b, (uint8_t)count)) {
    return false;
  }
  for (int i = 0; i < count; i++) {
    if (!ng_proto_write_state_body(b, &updates[i])) {
      return false;
    }
  }
  return true;
}

bool ng_proto_decode_state_batch(NgProtoBuf *b, NgStateUpdate *updates, int max_count,
                                 int *out_count) {
  if (!b || !updates || max_count <= 0 || !out_count) {
    return false;
  }
  uint8_t count = 0;
  if (!ng_proto_read_u8(b, &count)) {
    return false;
  }
  if (count > (uint8_t)max_count) {
    return false;
  }
  for (int i = 0; i < count; i++) {
    if (!ng_proto_read_state_body(b, &updates[i])) {
      return false;
    }
  }
  *out_count = count;
  return true;
}

// agent: composer-2.5 | 2026-07-28 | state ack encode decode | b7c8d9
bool ng_proto_encode_state_ack(NgProtoBuf *b, uint16_t seq, uint32_t entity_id, uint16_t ack_seq) {
  if (!b) {
    return false;
  }
  ng_proto_buf_init(b);
  NgProtoHeader h = {
      .magic = NG_PROTO_MAGIC,
      .version = NG_PROTO_VERSION,
      .channel = NG_CH_UNRELIABLE,
      .type = NG_PKT_STATE_ACK,
      .seq = seq,
      .tick = 0,
  };
  return ng_proto_write_header(b, &h) && ng_proto_write_u32(b, entity_id) &&
         ng_proto_write_u16(b, ack_seq);
}

bool ng_proto_decode_state_ack(NgProtoBuf *b, uint32_t *entity_id, uint16_t *ack_seq) {
  if (!b || !entity_id || !ack_seq) {
    return false;
  }
  return ng_proto_read_u32(b, entity_id) && ng_proto_read_u16(b, ack_seq);
}

// agent: composer-2.5 | 2026-07-29 | register wire encode decode | a3f1c2
bool ng_proto_encode_register(NgProtoBuf *b, uint16_t seq, const NgRegisterReq *req) {
  if (!b || !req) {
    return false;
  }
  ng_proto_buf_init(b);
  NgProtoHeader h = {
      .magic = NG_PROTO_MAGIC,
      .version = NG_PROTO_VERSION,
      .channel = NG_CH_RELIABLE,
      .type = NG_PKT_REGISTER,
      .seq = seq,
      .tick = 0,
  };
  const size_t name_len = strnlen(req->name, sizeof(req->name) - 1);
  if (!ng_proto_write_header(b, &h) || !ng_proto_write_u8(b, (uint8_t)name_len)) {
    return false;
  }
  for (size_t i = 0; i < name_len; i++) {
    if (!ng_proto_write_u8(b, (uint8_t)req->name[i])) {
      return false;
    }
  }
  return ng_proto_write_u16(b, req->proto_ver);
}

bool ng_proto_decode_register(NgProtoBuf *b, NgRegisterReq *req) {
  if (!b || !req) {
    return false;
  }
  memset(req, 0, sizeof(*req));
  uint8_t name_len = 0;
  if (!ng_proto_read_u8(b, &name_len) || name_len >= sizeof(req->name)) {
    return false;
  }
  for (uint8_t i = 0; i < name_len; i++) {
    uint8_t c = 0;
    if (!ng_proto_read_u8(b, &c)) {
      return false;
    }
    req->name[i] = (char)c;
  }
  req->name[name_len] = '\0';
  return ng_proto_read_u16(b, &req->proto_ver);
}

bool ng_proto_encode_register_ack(NgProtoBuf *b, uint16_t seq, const NgRegisterAck *ack) {
  if (!b || !ack) {
    return false;
  }
  ng_proto_buf_init(b);
  NgProtoHeader h = {
      .magic = NG_PROTO_MAGIC,
      .version = NG_PROTO_VERSION,
      .channel = NG_CH_RELIABLE,
      .type = NG_PKT_REGISTER_ACK,
      .seq = seq,
      .tick = 0,
  };
  return ng_proto_write_header(b, &h) && ng_proto_write_u8(b, ack->peer_id) &&
         ng_proto_write_u8(b, ack->role) && ng_proto_write_u16(b, ack->agent_port) &&
         ng_proto_write_u16(b, ack->root_game_port);
}

bool ng_proto_decode_register_ack(NgProtoBuf *b, NgRegisterAck *ack) {
  if (!b || !ack) {
    return false;
  }
  memset(ack, 0, sizeof(*ack));
  return ng_proto_read_u8(b, &ack->peer_id) && ng_proto_read_u8(b, &ack->role) &&
         ng_proto_read_u16(b, &ack->agent_port) && ng_proto_read_u16(b, &ack->root_game_port);
}

// agent: composer-2.5 | 2026-08-01 | lockstep action wire v13 | ded0c5
static bool ng_proto_write_lock_action(NgProtoBuf *b, const NgLockAction *a) {
  if (!b || !a) {
    return false;
  }
  if (!ng_proto_write_u8(b, a->present ? 1u : 0u)) {
    return false;
  }
  if (!a->present) {
    return true;
  }
  if (a->argc > NG_LOCK_ACTION_FLOATS) {
    return false;
  }
  if (!ng_proto_write_u16(b, a->id) || !ng_proto_write_u8(b, a->argc)) {
    return false;
  }
  for (uint8_t i = 0; i < a->argc; i++) {
    if (!ng_proto_write_f32(b, a->argv[i])) {
      return false;
    }
  }
  return true;
}

static bool ng_proto_read_lock_action(NgProtoBuf *b, NgLockAction *a) {
  if (!b || !a) {
    return false;
  }
  memset(a, 0, sizeof(*a));
  uint8_t present = 0;
  if (!ng_proto_read_u8(b, &present)) {
    return false;
  }
  if (!present) {
    return true;
  }
  a->present = 1;
  if (!ng_proto_read_u16(b, &a->id) || !ng_proto_read_u8(b, &a->argc) ||
      a->argc > NG_LOCK_ACTION_FLOATS) {
    return false;
  }
  for (uint8_t i = 0; i < a->argc; i++) {
    if (!ng_proto_read_f32(b, &a->argv[i])) {
      return false;
    }
  }
  return true;
}

// agent: composer-2.5 | 2026-07-29 | lockstep protocol packets | 30ad80
bool ng_proto_encode_lock_input(NgProtoBuf *b, uint16_t seq, const NgLockInputPkt *pkt) {
  if (!b || !pkt || pkt->count > NG_LOCK_INPUT_MAX) {
    return false;
  }
  ng_proto_buf_init(b);
  NgProtoHeader h = {
      .magic = NG_PROTO_MAGIC,
      .version = NG_PROTO_VERSION,
      .channel = NG_CH_UNRELIABLE,
      .type = NG_PKT_LOCK_INPUT,
      .seq = seq,
      .tick = pkt->base_tick,
  };
  if (!ng_proto_write_header(b, &h) || !ng_proto_write_u8(b, pkt->peer_id) ||
      !ng_proto_write_u32(b, pkt->base_tick) || !ng_proto_write_u8(b, pkt->count)) {
    return false;
  }
  for (uint8_t i = 0; i < pkt->count; i++) {
    if (!ng_proto_write_u8(b, pkt->bits[i]) || !ng_proto_write_lock_action(b, &pkt->actions[i])) {
      return false;
    }
  }
  return true;
}

bool ng_proto_decode_lock_input(NgProtoBuf *b, NgLockInputPkt *pkt) {
  if (!b || !pkt) {
    return false;
  }
  memset(pkt, 0, sizeof(*pkt));
  if (!ng_proto_read_u8(b, &pkt->peer_id) || !ng_proto_read_u32(b, &pkt->base_tick) ||
      !ng_proto_read_u8(b, &pkt->count) || pkt->count > NG_LOCK_INPUT_MAX) {
    return false;
  }
  for (uint8_t i = 0; i < pkt->count; i++) {
    if (!ng_proto_read_u8(b, &pkt->bits[i]) || !ng_proto_read_lock_action(b, &pkt->actions[i])) {
      return false;
    }
  }
  return true;
}

bool ng_proto_encode_lock_ack(NgProtoBuf *b, uint16_t seq, const NgLockAckPkt *pkt) {
  if (!b || !pkt) {
    return false;
  }
  ng_proto_buf_init(b);
  NgProtoHeader h = {
      .magic = NG_PROTO_MAGIC,
      .version = NG_PROTO_VERSION,
      .channel = NG_CH_UNRELIABLE,
      .type = NG_PKT_LOCK_ACK,
      .seq = seq,
      .tick = pkt->ack_tick,
  };
  return ng_proto_write_header(b, &h) && ng_proto_write_u8(b, pkt->peer_id) &&
         ng_proto_write_u32(b, pkt->ack_tick);
}

bool ng_proto_decode_lock_ack(NgProtoBuf *b, NgLockAckPkt *pkt) {
  if (!b || !pkt) {
    return false;
  }
  memset(pkt, 0, sizeof(*pkt));
  return ng_proto_read_u8(b, &pkt->peer_id) && ng_proto_read_u32(b, &pkt->ack_tick);
}

bool ng_proto_encode_lock_hash(NgProtoBuf *b, uint16_t seq, const NgLockHashPkt *pkt) {
  if (!b || !pkt) {
    return false;
  }
  ng_proto_buf_init(b);
  NgProtoHeader h = {
      .magic = NG_PROTO_MAGIC,
      .version = NG_PROTO_VERSION,
      .channel = NG_CH_UNRELIABLE,
      .type = NG_PKT_LOCK_HASH,
      .seq = seq,
      .tick = pkt->tick,
  };
  return ng_proto_write_header(b, &h) && ng_proto_write_u8(b, pkt->peer_id) &&
         ng_proto_write_u32(b, pkt->tick) && ng_proto_write_u32(b, pkt->hash);
}

bool ng_proto_decode_lock_hash(NgProtoBuf *b, NgLockHashPkt *pkt) {
  if (!b || !pkt) {
    return false;
  }
  memset(pkt, 0, sizeof(*pkt));
  return ng_proto_read_u8(b, &pkt->peer_id) && ng_proto_read_u32(b, &pkt->tick) &&
         ng_proto_read_u32(b, &pkt->hash);
}

// agent: composer-2.5 | 2026-07-29 | lockstep protocol packets | 30ad80
// agent: composer-2.5 | 2026-07-29 | proto v6 session encode decode | a782bd
// agent: composer-2.5 | 2026-07-28 | state ack encode decode | b7c8d9
// agent: composer-2.5 | 2026-07-28 | wire rad deg rot convert | ba07f0
// agent: composer-2.5 | 2026-07-29 | full-circle angle wire quant | a7e2c4

// agent: composer-2.5 | 2026-07-30 | proto v8 lock phys packets | 259e88
bool ng_proto_encode_lock_pause(NgProtoBuf *b, uint16_t seq, const NgLockPausePkt *pkt) {
  if (!b || !pkt) {
    return false;
  }
  ng_proto_buf_init(b);
  NgProtoHeader h = {
      .magic = NG_PROTO_MAGIC,
      .version = NG_PROTO_VERSION,
      .channel = NG_CH_RELIABLE,
      .type = NG_PKT_LOCK_PAUSE,
      .seq = seq,
      .tick = pkt->sim_tick,
  };
  return ng_proto_write_header(b, &h) && ng_proto_write_u32(b, pkt->sim_tick);
}

bool ng_proto_decode_lock_pause(NgProtoBuf *b, NgLockPausePkt *pkt) {
  if (!b || !pkt) {
    return false;
  }
  memset(pkt, 0, sizeof(*pkt));
  return ng_proto_read_u32(b, &pkt->sim_tick);
}

bool ng_proto_encode_lock_phys(NgProtoBuf *b, uint16_t seq, const NgLockPhysPkt *pkt) {
  if (!b || !pkt || pkt->len > NG_LOCK_PHYS_CHUNK) {
    return false;
  }
  ng_proto_buf_init(b);
  NgProtoHeader h = {
      .magic = NG_PROTO_MAGIC,
      .version = NG_PROTO_VERSION,
      .channel = NG_CH_RELIABLE,
      .type = NG_PKT_LOCK_PHYS,
      .seq = seq,
      .tick = pkt->sim_tick,
  };
  if (!ng_proto_write_header(b, &h) || !ng_proto_write_u32(b, pkt->sim_tick) ||
      !ng_proto_write_u32(b, pkt->offset) || !ng_proto_write_u32(b, pkt->total) ||
      !ng_proto_write_u32(b, pkt->world_hash) || !ng_proto_write_u16(b, pkt->len)) {
    return false;
  }
  for (uint16_t i = 0; i < pkt->len; i++) {
    if (!ng_proto_write_u8(b, pkt->data[i])) {
      return false;
    }
  }
  return true;
}

bool ng_proto_decode_lock_phys(NgProtoBuf *b, NgLockPhysPkt *pkt) {
  if (!b || !pkt) {
    return false;
  }
  memset(pkt, 0, sizeof(*pkt));
  // agent: composer-2.5 | 2026-07-31 | phys encode expect hash | ba2dd6
  if (!ng_proto_read_u32(b, &pkt->sim_tick) || !ng_proto_read_u32(b, &pkt->offset) ||
      !ng_proto_read_u32(b, &pkt->total) || !ng_proto_read_u32(b, &pkt->world_hash) ||
      !ng_proto_read_u16(b, &pkt->len) || pkt->len > NG_LOCK_PHYS_CHUNK) {
    return false;
  }
  for (uint16_t i = 0; i < pkt->len; i++) {
    if (!ng_proto_read_u8(b, &pkt->data[i])) {
      return false;
    }
  }
  return true;
}

bool ng_proto_encode_lock_ready(NgProtoBuf *b, uint16_t seq, const NgLockReadyPkt *pkt) {
  if (!b || !pkt) {
    return false;
  }
  ng_proto_buf_init(b);
  NgProtoHeader h = {
      .magic = NG_PROTO_MAGIC,
      .version = NG_PROTO_VERSION,
      .channel = NG_CH_RELIABLE,
      .type = NG_PKT_LOCK_READY,
      .seq = seq,
      .tick = pkt->sim_tick,
  };
  return ng_proto_write_header(b, &h) && ng_proto_write_u8(b, pkt->peer_id) &&
         ng_proto_write_u32(b, pkt->sim_tick) && ng_proto_write_u32(b, pkt->hash);
}

bool ng_proto_decode_lock_ready(NgProtoBuf *b, NgLockReadyPkt *pkt) {
  if (!b || !pkt) {
    return false;
  }
  memset(pkt, 0, sizeof(*pkt));
  return ng_proto_read_u8(b, &pkt->peer_id) && ng_proto_read_u32(b, &pkt->sim_tick) &&
         ng_proto_read_u32(b, &pkt->hash);
}

bool ng_proto_encode_lock_resume(NgProtoBuf *b, uint16_t seq, const NgLockResumePkt *pkt) {
  if (!b || !pkt || pkt->peer_count > NG_LOCK_PEER_MAX) {
    return false;
  }
  ng_proto_buf_init(b);
  NgProtoHeader h = {
      .magic = NG_PROTO_MAGIC,
      .version = NG_PROTO_VERSION,
      .channel = NG_CH_RELIABLE,
      .type = NG_PKT_LOCK_RESUME,
      .seq = seq,
      .tick = pkt->sim_tick,
  };
  if (!ng_proto_write_header(b, &h) || !ng_proto_write_u32(b, pkt->sim_tick) ||
      !ng_proto_write_u8(b, pkt->peer_count)) {
    return false;
  }
  for (uint8_t i = 0; i < pkt->peer_count; i++) {
    if (!ng_proto_write_u8(b, pkt->peer_ids[i])) {
      return false;
    }
  }
  return true;
}

bool ng_proto_decode_lock_resume(NgProtoBuf *b, NgLockResumePkt *pkt) {
  if (!b || !pkt) {
    return false;
  }
  memset(pkt, 0, sizeof(*pkt));
  if (!ng_proto_read_u32(b, &pkt->sim_tick) || !ng_proto_read_u8(b, &pkt->peer_count) ||
      pkt->peer_count > NG_LOCK_PEER_MAX) {
    return false;
  }
  for (uint8_t i = 0; i < pkt->peer_count; i++) {
    if (!ng_proto_read_u8(b, &pkt->peer_ids[i])) {
      return false;
    }
  }
  return true;
}

// agent: composer-2.5 | 2026-07-31 | lock confirm encode decode | de86c1
bool ng_proto_encode_lock_confirm(NgProtoBuf *b, uint16_t seq, const NgLockConfirmPkt *pkt) {
  if (!b || !pkt || pkt->peer_count > NG_LOCK_PEER_MAX) {
    return false;
  }
  ng_proto_buf_init(b);
  NgProtoHeader h = {
      .magic = NG_PROTO_MAGIC,
      .version = NG_PROTO_VERSION,
      .channel = NG_CH_UNRELIABLE,
      .type = NG_PKT_LOCK_CONFIRM,
      .seq = seq,
      .tick = pkt->tick,
  };
  if (!ng_proto_write_header(b, &h) || !ng_proto_write_u32(b, pkt->tick) ||
      !ng_proto_write_u8(b, pkt->peer_count) || !ng_proto_write_u8(b, pkt->miss_mask)) {
    return false;
  }
  for (uint8_t i = 0; i < pkt->peer_count; i++) {
    if (!ng_proto_write_u8(b, pkt->peer_ids[i]) || !ng_proto_write_u8(b, pkt->bits[i]) ||
        !ng_proto_write_lock_action(b, &pkt->actions[i])) {
      return false;
    }
  }
  return true;
}

bool ng_proto_decode_lock_confirm(NgProtoBuf *b, NgLockConfirmPkt *pkt) {
  if (!b || !pkt) {
    return false;
  }
  memset(pkt, 0, sizeof(*pkt));
  if (!ng_proto_read_u32(b, &pkt->tick) || !ng_proto_read_u8(b, &pkt->peer_count) ||
      !ng_proto_read_u8(b, &pkt->miss_mask) || pkt->peer_count > NG_LOCK_PEER_MAX) {
    return false;
  }
  for (uint8_t i = 0; i < pkt->peer_count; i++) {
    if (!ng_proto_read_u8(b, &pkt->peer_ids[i]) || !ng_proto_read_u8(b, &pkt->bits[i]) ||
        !ng_proto_read_lock_action(b, &pkt->actions[i])) {
      return false;
    }
  }
  return true;
}
// agent: composer-2.5 | 2026-07-30 | proto quantize lin ang vel | b9fdec
// agent: composer-2.5 | 2026-07-31 | phys encode expect hash | ba2dd6
// agent: composer-2.5 | 2026-08-01 | session playout encode | 5be998
// agent: composer-2.5 | 2026-08-01 | session mode wire byte | de3bcd
// agent: composer-2.5 | 2026-08-01 | smallest three quat wire | b4fc42
// agent: composer-2.5 | 2026-08-01 | proto version 12 | c8cd04
// agent: composer-2.5 | 2026-08-01 | lockstep action wire v13 | ded0c5
