// agent: composer-2.5 | 2026-07-27 | spawn registry and routing | e2f3a4
// agent: composer-2.5 | 2026-07-29 | graph uses active runtime | 48c6e0
// agent: composer-2.5 | 2026-07-29 | instance primary spawn registry | 9c2f5c
#include "graph.h"
#include "scene/runtime.h"
#include "world/ng_world.h"
#include "vendor/duktape.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define GGRAPH() (*mod_scene_runtime_graph())

static NgSceneDesc *mod_scene_graph_find_desc(const char *kind, const char *name) {
  for (int i = 0; i < GGRAPH().desc_count; i++) {
    NgSceneDesc *d = &GGRAPH().descs[i];
    if (d->alive && strcmp(d->kind, kind) == 0 && strcmp(d->name, name) == 0) {
      return d;
    }
  }
  return NULL;
}

void mod_scene_graph_reset(void) {
  ModSceneGraphCtx *g = mod_scene_runtime_graph();
  memset(g, 0, sizeof(*g));
  g->next_local_id = 1u;
}

bool mod_scene_graph_describe(const char *kind, const char *name, NgSyncMode sync,
                              const char *model, const char *body, int func_stash_idx) {
  if (!kind || !name) {
    return false;
  }
  NgSceneDesc *existing = mod_scene_graph_find_desc(kind, name);
  if (existing) {
    existing->sync = sync;
    existing->func_stash_idx = func_stash_idx;
    if (model) {
      strncpy(existing->model, model, sizeof(existing->model) - 1);
    }
    // agent: composer-2.5 | 2026-07-29 | entity optional body field | a88872
    if (body) {
      strncpy(existing->body, body, sizeof(existing->body) - 1);
    } else {
      existing->body[0] = '\0';
    }
    return true;
  }
  if (GGRAPH().desc_count >= NG_SCENE_DESC_MAX) {
    return false;
  }
  NgSceneDesc *d = &GGRAPH().descs[GGRAPH().desc_count++];
  memset(d, 0, sizeof(*d));
  d->alive = true;
  strncpy(d->kind, kind, sizeof(d->kind) - 1);
  strncpy(d->name, name, sizeof(d->name) - 1);
  d->sync = sync;
  d->func_stash_idx = func_stash_idx;
  if (model) {
    strncpy(d->model, model, sizeof(d->model) - 1);
  }
  if (body) {
    strncpy(d->body, body, sizeof(d->body) - 1);
  }
  return true;
}

NgSceneDesc *mod_scene_graph_entity_desc(const char *name) {
  return mod_scene_graph_find_desc("entity", name);
}

bool mod_scene_graph_dispose_desc(const char *kind, const char *name) {
  NgSceneDesc *d = mod_scene_graph_find_desc(kind, name);
  if (!d) {
    return false;
  }
  d->alive = false;
  return true;
}

uint32_t mod_scene_graph_alloc_id(void) { return GGRAPH().next_local_id++; }

NgSceneRegistry *mod_scene_graph_registry_by_id(uint32_t entity_id) {
  if (entity_id == 0) {
    return NULL;
  }
  for (int i = 0; i < GGRAPH().registry_count; i++) {
    NgSceneRegistry *r = &GGRAPH().registry[i];
    if (r->alive && r->entity_id == entity_id) {
      return r;
    }
  }
  return NULL;
}

NgSceneRegistry *mod_scene_graph_registry_by_key(const char *key) {
  if (!key || key[0] == '\0') {
    return NULL;
  }
  for (int i = 0; i < GGRAPH().registry_count; i++) {
    NgSceneRegistry *r = &GGRAPH().registry[i];
    if (r->alive && r->key[0] != '\0' && strcmp(r->key, key) == 0) {
      return r;
    }
  }
  return NULL;
}

bool mod_scene_graph_registry_add_instance(const char *desc_name, const char *key,
                                           uint32_t entity_id, NgSyncMode sync, const float pos[3],
                                           const float rot[3], float scale) {
  if (!desc_name || entity_id == 0) {
    return false;
  }
  NgSceneRegistry *existing = mod_scene_graph_registry_by_id(entity_id);
  if (existing) {
    strncpy(existing->desc_name, desc_name, sizeof(existing->desc_name) - 1);
    if (key) {
      strncpy(existing->key, key, sizeof(existing->key) - 1);
    }
    existing->sync = sync;
    if (pos) {
      existing->pos[0] = pos[0];
      existing->pos[1] = pos[1];
      existing->pos[2] = pos[2];
    }
    if (rot) {
      existing->rot[0] = rot[0];
      existing->rot[1] = rot[1];
      existing->rot[2] = rot[2];
    }
    existing->scale = scale > 0.0f ? scale : 1.0f;
    return true;
  }
  if (GGRAPH().registry_count >= NG_SCENE_INST_MAX) {
    return false;
  }
  NgSceneRegistry *r = &GGRAPH().registry[GGRAPH().registry_count++];
  memset(r, 0, sizeof(*r));
  r->alive = true;
  strncpy(r->desc_name, desc_name, sizeof(r->desc_name) - 1);
  if (key) {
    strncpy(r->key, key, sizeof(r->key) - 1);
  }
  r->entity_id = entity_id;
  r->sync = sync;
  if (pos) {
    r->pos[0] = pos[0];
    r->pos[1] = pos[1];
    r->pos[2] = pos[2];
  }
  if (rot) {
    r->rot[0] = rot[0];
    r->rot[1] = rot[1];
    r->rot[2] = rot[2];
  }
  r->scale = scale > 0.0f ? scale : 1.0f;
  return true;
}

void mod_scene_graph_registry_set_pose(uint32_t entity_id, const float pos[3], const float rot[3],
                                       float scale) {
  NgSceneRegistry *r = mod_scene_graph_registry_by_id(entity_id);
  if (!r) {
    return;
  }
  if (pos) {
    r->pos[0] = pos[0];
    r->pos[1] = pos[1];
    r->pos[2] = pos[2];
  }
  if (rot) {
    r->rot[0] = rot[0];
    r->rot[1] = rot[1];
    r->rot[2] = rot[2];
  }
  if (scale > 0.0f) {
    r->scale = scale;
  }
}

void mod_scene_graph_registry_clear_id(uint32_t entity_id) {
  NgSceneRegistry *r = mod_scene_graph_registry_by_id(entity_id);
  if (r) {
    r->alive = false;
  }
}

void mod_scene_graph_fill_session_spawns(NgSessionState *session) {
  if (!session) {
    return;
  }
  session->spawn_count = 0;
  for (int i = 0; i < GGRAPH().registry_count && session->spawn_count < NG_SESSION_SPAWN_MAX; i++) {
    NgSceneRegistry *r = &GGRAPH().registry[i];
    if (!r->alive || r->sync == NG_SYNC_LOCAL) {
      continue;
    }
    NgSceneInst *inst = mod_scene_graph_inst_by_id(r->entity_id);
    NgSessionSpawn *sp = &session->spawns[session->spawn_count++];
    memset(sp, 0, sizeof(*sp));
    sp->entity_id = r->entity_id;
    strncpy(sp->desc_name, r->desc_name, sizeof(sp->desc_name) - 1);
    strncpy(sp->key, r->key, sizeof(sp->key) - 1);
    sp->sync = r->sync;
    if (inst) {
      sp->pos[0] = inst->pos[0];
      sp->pos[1] = inst->pos[1];
      sp->pos[2] = inst->pos[2];
      sp->rot[0] = inst->rot[0];
      sp->rot[1] = inst->rot[1];
      sp->rot[2] = inst->rot[2];
      sp->scale = inst->scale;
      strncpy(sp->key, inst->key, sizeof(sp->key) - 1);
    } else {
      sp->pos[0] = r->pos[0];
      sp->pos[1] = r->pos[1];
      sp->pos[2] = r->pos[2];
      sp->rot[0] = r->rot[0];
      sp->rot[1] = r->rot[1];
      sp->rot[2] = r->rot[2];
      sp->scale = r->scale > 0.0f ? r->scale : 1.0f;
    }
  }
  // agent: composer-2.5 | 2026-07-29 | stable spawn key order | 091bfd
  for (int i = 0; i < session->spawn_count; i++) {
    for (int j = i + 1; j < session->spawn_count; j++) {
      if (strcmp(session->spawns[j].key, session->spawns[i].key) < 0) {
        NgSessionSpawn tmp = session->spawns[i];
        session->spawns[i] = session->spawns[j];
        session->spawns[j] = tmp;
      }
    }
  }
}

void mod_scene_graph_seed_pending(const NgSessionState *session) {
  GGRAPH().pending_count = 0;
  memset(GGRAPH().pending_matched, 0, sizeof(GGRAPH().pending_matched));
  if (!session) {
    return;
  }
  for (int i = 0; i < session->spawn_count && i < NG_SESSION_SPAWN_MAX; i++) {
    GGRAPH().pending[i] = session->spawns[i];
    GGRAPH().pending_matched[i] = 0;
  }
  GGRAPH().pending_count = session->spawn_count < NG_SESSION_SPAWN_MAX ? session->spawn_count
                                                                      : NG_SESSION_SPAWN_MAX;
}

const NgSessionSpawn *mod_scene_graph_take_pending(const char *desc_name, const char *key) {
  if (!desc_name) {
    return NULL;
  }
  if (key && key[0] != '\0') {
    for (int i = 0; i < GGRAPH().pending_count; i++) {
      if (GGRAPH().pending_matched[i]) {
        continue;
      }
      NgSessionSpawn *sp = &GGRAPH().pending[i];
      if (sp->key[0] != '\0' && strcmp(sp->key, key) == 0) {
        GGRAPH().pending_matched[i] = 1;
        return sp;
      }
    }
    return NULL;
  }
  for (int i = 0; i < GGRAPH().pending_count; i++) {
    if (GGRAPH().pending_matched[i]) {
      continue;
    }
    NgSessionSpawn *sp = &GGRAPH().pending[i];
    if (strcmp(sp->desc_name, desc_name) == 0) {
      GGRAPH().pending_matched[i] = 1;
      return sp;
    }
  }
  return NULL;
}

void mod_scene_graph_foreach_unmatched_pending(void (*fn)(const NgSessionSpawn *sp, void *ud),
                                               void *ud) {
  if (!fn) {
    return;
  }
  for (int i = 0; i < GGRAPH().pending_count; i++) {
    if (GGRAPH().pending_matched[i]) {
      continue;
    }
    fn(&GGRAPH().pending[i], ud);
  }
}

int mod_scene_graph_spawn(const char *desc_name, uint32_t entity_id, const char *key,
                          const float pos[3], const float rot[3], float scale, duk_context *ctx,
                          int func_stash_idx) {
  if (!desc_name) {
    return 0;
  }
  NgSceneDesc *d = mod_scene_graph_entity_desc(desc_name);
  if (!d) {
    return 0;
  }
  if (entity_id != 0) {
    NgSceneInst *existing = mod_scene_graph_inst_by_id(entity_id);
    if (existing) {
      return existing->handle;
    }
  }
  if (key && key[0] != '\0') {
    NgSceneInst *by_key = mod_scene_graph_inst_by_key(key);
    if (by_key) {
      return by_key->handle;
    }
  }
  if (GGRAPH().inst_count >= NG_SCENE_INST_MAX) {
    return 0;
  }
  NgSceneInst *inst = &GGRAPH().insts[GGRAPH().inst_count++];
  memset(inst, 0, sizeof(*inst));
  inst->alive = true;
  inst->handle = GGRAPH().inst_count;
  inst->id = entity_id ? entity_id : mod_scene_graph_alloc_id();
  strncpy(inst->desc_name, desc_name, sizeof(inst->desc_name) - 1);
  if (key) {
    strncpy(inst->key, key, sizeof(inst->key) - 1);
  }
  strncpy(inst->model, d->model, sizeof(inst->model) - 1);
  // agent: composer-2.5 | 2026-07-29 | entity optional body field | a88872
  strncpy(inst->body, d->body, sizeof(inst->body) - 1);
  inst->body_id_bits = 0;
  inst->sync = d->sync;
  if (pos) {
    inst->pos[0] = pos[0];
    inst->pos[1] = pos[1];
    inst->pos[2] = pos[2];
  }
  if (rot) {
    inst->rot[0] = rot[0];
    inst->rot[1] = rot[1];
    inst->rot[2] = rot[2];
  }
  inst->scale = scale > 0.0f ? scale : 1.0f;
  inst->script_inst_stash = -1;
  mod_scene_graph_registry_add_instance(desc_name, key, inst->id, d->sync, inst->pos, inst->rot,
                                        inst->scale);

  if (ctx && func_stash_idx >= 0) {
    // agent: composer-2.5 | 2026-07-30 | fix graph spawn stash leak | cddf4f
    duk_push_global_stash(ctx);
    char func_key[48];
    snprintf(func_key, sizeof(func_key), "func_%s", desc_name);
    duk_get_prop_string(ctx, -1, func_key);
    if (duk_is_function(ctx, -1)) {
      duk_new(ctx, 0);
      duk_push_int(ctx, inst->handle);
      duk_put_prop_string(ctx, -2, "handle");
      char inst_key[48];
      snprintf(inst_key, sizeof(inst_key), "inst_%d", inst->handle);
      duk_push_global_stash(ctx);
      duk_insert(ctx, -2);
      duk_put_prop_string(ctx, -2, inst_key);
      duk_pop_n(ctx, 2); /* stash2 + stash1 */
    } else {
      duk_pop_n(ctx, 2);
    }
  }
  return inst->handle;
}

bool mod_scene_graph_despawn(int handle) {
  NgSceneInst *inst = mod_scene_graph_inst_by_handle(handle);
  if (!inst) {
    return false;
  }
  mod_scene_graph_registry_clear_id(inst->id);
  inst->alive = false;
  return true;
}

NgSceneInst *mod_scene_graph_inst_by_handle(int handle) {
  if (handle <= 0) {
    return NULL;
  }
  for (int i = 0; i < GGRAPH().inst_count; i++) {
    if (GGRAPH().insts[i].alive && GGRAPH().insts[i].handle == handle) {
      return &GGRAPH().insts[i];
    }
  }
  return NULL;
}

NgSceneInst *mod_scene_graph_inst_by_id(uint32_t entity_id) {
  for (int i = 0; i < GGRAPH().inst_count; i++) {
    if (GGRAPH().insts[i].alive && GGRAPH().insts[i].id == entity_id) {
      return &GGRAPH().insts[i];
    }
  }
  return NULL;
}

NgSceneInst *mod_scene_graph_inst_by_key(const char *key) {
  if (!key || key[0] == '\0') {
    return NULL;
  }
  for (int i = 0; i < GGRAPH().inst_count; i++) {
    NgSceneInst *inst = &GGRAPH().insts[i];
    if (inst->alive && inst->key[0] != '\0' && strcmp(inst->key, key) == 0) {
      return inst;
    }
  }
  return NULL;
}

NgSceneInst *mod_scene_graph_inst_by_desc(const char *desc_name) {
  if (!desc_name) {
    return NULL;
  }
  for (int i = 0; i < GGRAPH().inst_count; i++) {
    NgSceneInst *inst = &GGRAPH().insts[i];
    if (inst->alive && strcmp(inst->desc_name, desc_name) == 0) {
      return inst;
    }
  }
  return NULL;
}

NgSyncMode mod_scene_graph_sync_for_entity(uint32_t entity_id) {
  NgSceneInst *inst = mod_scene_graph_inst_by_id(entity_id);
  if (inst) {
    return inst->sync;
  }
  NgSceneRegistry *r = mod_scene_graph_registry_by_id(entity_id);
  if (r) {
    return r->sync;
  }
  return NG_SYNC_SERVER;
}

void mod_scene_graph_mark_dirty(NgSceneInst *inst, uint32_t comp) {
  if (inst) {
    inst->comp_dirty |= comp;
  }
}

bool mod_scene_graph_take_dirty(NgStateUpdate *out) {
  // agent: composer-2.5 | 2026-07-30 | take dirty apply vel | 22b8f6
  if (!out) {
    return false;
  }
  for (int i = 0; i < GGRAPH().inst_count; i++) {
    NgSceneInst *inst = &GGRAPH().insts[i];
    if (!inst->alive || inst->comp_dirty == 0) {
      continue;
    }
    if (!ng_sync_posts_wire(inst->sync)) {
      inst->comp_dirty = 0;
      continue;
    }
    const uint32_t wire_mask =
        inst->comp_dirty &
        (NG_COMP_POS | NG_COMP_ROT | NG_COMP_SCALE | NG_COMP_LIN_VEL | NG_COMP_ANG_VEL);
    if (wire_mask == 0) {
      inst->comp_dirty = 0;
      continue;
    }
    out->entity_id = inst->id;
    out->seq = ++GGRAPH().next_seq;
    inst->last_sent_seq = out->seq;
    out->comp_mask = (uint8_t)wire_mask;
    out->tick = 0;
    if (wire_mask & NG_COMP_POS) {
      out->pos[0] = inst->pos[0];
      out->pos[1] = inst->pos[1];
      out->pos[2] = inst->pos[2];
    }
    if (wire_mask & NG_COMP_ROT) {
      out->rot[0] = inst->rot[0];
      out->rot[1] = inst->rot[1];
      out->rot[2] = inst->rot[2];
    }
    if (wire_mask & NG_COMP_SCALE) {
      out->scale = inst->scale;
    }
    if (wire_mask & NG_COMP_LIN_VEL) {
      out->lin_vel[0] = inst->lin_vel[0];
      out->lin_vel[1] = inst->lin_vel[1];
      out->lin_vel[2] = inst->lin_vel[2];
    }
    if (wire_mask & NG_COMP_ANG_VEL) {
      out->ang_vel[0] = inst->ang_vel[0];
      out->ang_vel[1] = inst->ang_vel[1];
      out->ang_vel[2] = inst->ang_vel[2];
    }
    inst->comp_dirty &= ~wire_mask;
    return true;
  }
  return false;
}

void mod_scene_graph_apply_update(const NgStateUpdate *update) {
  if (!update) {
    return;
  }
  NgSceneInst *inst = mod_scene_graph_inst_by_id(update->entity_id);
  // agent: composer-2.5 | 2026-07-29 | drop last_sent echo filter | e1c9a2
  if (inst) {
    if (inst->last_applied_seq != 0 &&
        (uint16_t)(update->seq - inst->last_applied_seq) > 0x8000u) {
      return;
    }
    if (update->seq == inst->last_applied_seq) {
      return;
    }
    inst->last_applied_seq = update->seq;
    // agent: composer-2.5 | 2026-07-30 | take dirty apply vel | 22b8f6
    // agent: composer-2.5 | 2026-07-30 | ack baseline encode apply | 1a55f8
    const bool is_delta = (update->comp_mask & NG_COMP_FLAGS) != 0;
    if (update->comp_mask & NG_COMP_POS) {
      if (is_delta) {
        inst->pos[0] += update->pos[0];
        inst->pos[1] += update->pos[1];
        inst->pos[2] += update->pos[2];
      } else {
        inst->pos[0] = update->pos[0];
        inst->pos[1] = update->pos[1];
        inst->pos[2] = update->pos[2];
      }
    }
    if (update->comp_mask & NG_COMP_ROT) {
      if (is_delta) {
        inst->rot[0] += update->rot[0];
        inst->rot[1] += update->rot[1];
        inst->rot[2] += update->rot[2];
      } else {
        inst->rot[0] = update->rot[0];
        inst->rot[1] = update->rot[1];
        inst->rot[2] = update->rot[2];
      }
    }
    if (update->comp_mask & NG_COMP_SCALE) {
      if (is_delta) {
        inst->scale += update->scale;
      } else {
        inst->scale = update->scale;
      }
    }
    if (update->comp_mask & NG_COMP_LIN_VEL) {
      if (is_delta) {
        inst->lin_vel[0] += update->lin_vel[0];
        inst->lin_vel[1] += update->lin_vel[1];
        inst->lin_vel[2] += update->lin_vel[2];
      } else {
        inst->lin_vel[0] = update->lin_vel[0];
        inst->lin_vel[1] = update->lin_vel[1];
        inst->lin_vel[2] = update->lin_vel[2];
      }
    }
    if (update->comp_mask & NG_COMP_ANG_VEL) {
      if (is_delta) {
        inst->ang_vel[0] += update->ang_vel[0];
        inst->ang_vel[1] += update->ang_vel[1];
        inst->ang_vel[2] += update->ang_vel[2];
      } else {
        inst->ang_vel[0] = update->ang_vel[0];
        inst->ang_vel[1] = update->ang_vel[1];
        inst->ang_vel[2] = update->ang_vel[2];
      }
    }
  }
  float pos[3] = {0}, rot[3] = {0};
  float scale = 0.0f;
  const float *p = NULL;
  const float *r = NULL;
  if (inst) {
    p = inst->pos;
    r = inst->rot;
    scale = inst->scale;
  } else {
    NgSceneRegistry *reg = mod_scene_graph_registry_by_id(update->entity_id);
    if (!reg) {
      return;
    }
    pos[0] = reg->pos[0];
    pos[1] = reg->pos[1];
    pos[2] = reg->pos[2];
    rot[0] = reg->rot[0];
    rot[1] = reg->rot[1];
    rot[2] = reg->rot[2];
    scale = reg->scale;
    if (update->comp_mask & NG_COMP_POS) {
      pos[0] = update->pos[0];
      pos[1] = update->pos[1];
      pos[2] = update->pos[2];
    }
    if (update->comp_mask & NG_COMP_ROT) {
      rot[0] = update->rot[0];
      rot[1] = update->rot[1];
      rot[2] = update->rot[2];
    }
    if (update->comp_mask & NG_COMP_SCALE) {
      scale = update->scale;
    }
    p = pos;
    r = rot;
  }
  mod_scene_graph_registry_set_pose(update->entity_id, p, r, scale);
}

// agent: composer-2.5 | 2026-07-30 | push state sample ring | 19d0cd
void mod_scene_graph_push_sample(NgSceneInst *inst, double t) {
  if (!inst) {
    return;
  }
  NgStateSample *s = &inst->samples[inst->sample_head % NG_STATE_SAMPLE_MAX];
  s->t = t;
  s->pos[0] = inst->pos[0];
  s->pos[1] = inst->pos[1];
  s->pos[2] = inst->pos[2];
  s->rot[0] = inst->rot[0];
  s->rot[1] = inst->rot[1];
  s->rot[2] = inst->rot[2];
  s->lin_vel[0] = inst->lin_vel[0];
  s->lin_vel[1] = inst->lin_vel[1];
  s->lin_vel[2] = inst->lin_vel[2];
  s->ang_vel[0] = inst->ang_vel[0];
  s->ang_vel[1] = inst->ang_vel[1];
  s->ang_vel[2] = inst->ang_vel[2];
  inst->sample_head = (uint8_t)((inst->sample_head + 1u) % NG_STATE_SAMPLE_MAX);
  if (inst->sample_count < NG_STATE_SAMPLE_MAX) {
    inst->sample_count++;
  }
  inst->state_time = t;
}

static const NgStateSample *mod_scene_graph_sample_at(const NgSceneInst *inst, int chron_idx) {
  /* chron_idx 0 = oldest, sample_count-1 = newest */
  if (!inst || chron_idx < 0 || chron_idx >= (int)inst->sample_count) {
    return NULL;
  }
  const int oldest =
      (inst->sample_head + NG_STATE_SAMPLE_MAX - (int)inst->sample_count) % NG_STATE_SAMPLE_MAX;
  return &inst->samples[(oldest + chron_idx) % NG_STATE_SAMPLE_MAX];
}

static float mod_scene_graph_hermite1(float p0, float v0, float p1, float v1, float dt, float a) {
  const float a2 = a * a;
  const float a3 = a2 * a;
  const float h00 = 2.0f * a3 - 3.0f * a2 + 1.0f;
  const float h10 = a3 - 2.0f * a2 + a;
  const float h01 = -2.0f * a3 + 3.0f * a2;
  const float h11 = a3 - a2;
  return h00 * p0 + h10 * dt * v0 + h01 * p1 + h11 * dt * v1;
}

bool mod_scene_graph_sample_draw_pose(const NgSceneInst *inst, double now, float delay_s,
                                      float out_pos[3], float out_rot[3]) {
  if (!inst || !out_pos || !out_rot) {
    return false;
  }
  out_pos[0] = inst->pos[0];
  out_pos[1] = inst->pos[1];
  out_pos[2] = inst->pos[2];
  out_rot[0] = inst->rot[0];
  out_rot[1] = inst->rot[1];
  out_rot[2] = inst->rot[2];
  if (inst->sample_count == 0) {
    return false;
  }
  const double render_t = now - (double)delay_s;
  const NgStateSample *newest = mod_scene_graph_sample_at(inst, (int)inst->sample_count - 1);
  const NgStateSample *oldest = mod_scene_graph_sample_at(inst, 0);
  if (!newest || !oldest) {
    return false;
  }
  if (inst->sample_count == 1 || render_t >= newest->t) {
    float age = (float)(now - newest->t);
    if (age < 0.0f) {
      age = 0.0f;
    }
    if (age > delay_s) {
      age = delay_s;
    }
    out_pos[0] = newest->pos[0] + newest->lin_vel[0] * age;
    out_pos[1] = newest->pos[1] + newest->lin_vel[1] * age;
    out_pos[2] = newest->pos[2] + newest->lin_vel[2] * age;
    out_rot[0] = newest->rot[0] + newest->ang_vel[0] * age;
    out_rot[1] = newest->rot[1] + newest->ang_vel[1] * age;
    out_rot[2] = newest->rot[2] + newest->ang_vel[2] * age;
    return true;
  }
  if (render_t <= oldest->t) {
    out_pos[0] = oldest->pos[0];
    out_pos[1] = oldest->pos[1];
    out_pos[2] = oldest->pos[2];
    out_rot[0] = oldest->rot[0];
    out_rot[1] = oldest->rot[1];
    out_rot[2] = oldest->rot[2];
    return true;
  }
  const NgStateSample *a = oldest;
  const NgStateSample *b = newest;
  for (int i = 0; i < (int)inst->sample_count - 1; i++) {
    const NgStateSample *s0 = mod_scene_graph_sample_at(inst, i);
    const NgStateSample *s1 = mod_scene_graph_sample_at(inst, i + 1);
    if (s0 && s1 && render_t >= s0->t && render_t <= s1->t) {
      a = s0;
      b = s1;
      break;
    }
  }
  float dt = (float)(b->t - a->t);
  if (dt < 1e-4f) {
    out_pos[0] = b->pos[0];
    out_pos[1] = b->pos[1];
    out_pos[2] = b->pos[2];
    out_rot[0] = b->rot[0];
    out_rot[1] = b->rot[1];
    out_rot[2] = b->rot[2];
    return true;
  }
  const float alpha = (float)((render_t - a->t) / (double)dt);
  for (int i = 0; i < 3; i++) {
    out_pos[i] = mod_scene_graph_hermite1(a->pos[i], a->lin_vel[i], b->pos[i], b->lin_vel[i], dt,
                                          alpha);
    out_rot[i] = mod_scene_graph_hermite1(a->rot[i], a->ang_vel[i], b->rot[i], b->ang_vel[i], dt,
                                          alpha);
  }
  return true;
}

// agent: composer-2.5 | 2026-07-30 | ack baseline encode apply | 1a55f8
void mod_scene_graph_note_sent(NgSceneInst *inst, const NgStateUpdate *update) {
  if (!inst || !update) {
    return;
  }
  inst->last_sent_seq = update->seq;
  inst->last_sent_pos[0] = inst->pos[0];
  inst->last_sent_pos[1] = inst->pos[1];
  inst->last_sent_pos[2] = inst->pos[2];
  inst->last_sent_rot[0] = inst->rot[0];
  inst->last_sent_rot[1] = inst->rot[1];
  inst->last_sent_rot[2] = inst->rot[2];
  inst->last_sent_scale = inst->scale;
  inst->last_sent_lin_vel[0] = inst->lin_vel[0];
  inst->last_sent_lin_vel[1] = inst->lin_vel[1];
  inst->last_sent_lin_vel[2] = inst->lin_vel[2];
  inst->last_sent_ang_vel[0] = inst->ang_vel[0];
  inst->last_sent_ang_vel[1] = inst->ang_vel[1];
  inst->last_sent_ang_vel[2] = inst->ang_vel[2];
}

void mod_scene_graph_note_ack(uint32_t entity_id, uint16_t ack_seq) {
  NgSceneInst *inst = mod_scene_graph_inst_by_id(entity_id);
  if (!inst) {
    return;
  }
  /* Accept ack for the last sent seq (or any not-newer wrap-safe match). */
  if (inst->last_sent_seq == 0) {
    return;
  }
  if (ack_seq != inst->last_sent_seq &&
      (uint16_t)(inst->last_sent_seq - ack_seq) > 0x8000u) {
    return;
  }
  inst->have_wire_ack = true;
  inst->ack_pos[0] = inst->last_sent_pos[0];
  inst->ack_pos[1] = inst->last_sent_pos[1];
  inst->ack_pos[2] = inst->last_sent_pos[2];
  inst->ack_rot[0] = inst->last_sent_rot[0];
  inst->ack_rot[1] = inst->last_sent_rot[1];
  inst->ack_rot[2] = inst->last_sent_rot[2];
  inst->ack_scale = inst->last_sent_scale;
  inst->ack_lin_vel[0] = inst->last_sent_lin_vel[0];
  inst->ack_lin_vel[1] = inst->last_sent_lin_vel[1];
  inst->ack_lin_vel[2] = inst->last_sent_lin_vel[2];
  inst->ack_ang_vel[0] = inst->last_sent_ang_vel[0];
  inst->ack_ang_vel[1] = inst->last_sent_ang_vel[1];
  inst->ack_ang_vel[2] = inst->last_sent_ang_vel[2];
}

bool mod_scene_graph_prepare_wire_update(NgSceneInst *inst, NgStateUpdate *inout) {
  if (!inst || !inout) {
    return false;
  }
  /* Absolute if no ack baseline; else FLAGS marks delta from ack. */
  if (!inst->have_wire_ack) {
    inout->comp_mask = (uint8_t)(inout->comp_mask & (uint8_t)~NG_COMP_FLAGS);
    return true;
  }
  const uint8_t mask = inout->comp_mask;
  NgStateUpdate d = *inout;
  d.comp_mask = (uint8_t)(mask | NG_COMP_FLAGS);
  if (mask & NG_COMP_POS) {
    d.pos[0] = inst->pos[0] - inst->ack_pos[0];
    d.pos[1] = inst->pos[1] - inst->ack_pos[1];
    d.pos[2] = inst->pos[2] - inst->ack_pos[2];
  }
  if (mask & NG_COMP_ROT) {
    d.rot[0] = inst->rot[0] - inst->ack_rot[0];
    d.rot[1] = inst->rot[1] - inst->ack_rot[1];
    d.rot[2] = inst->rot[2] - inst->ack_rot[2];
  }
  if (mask & NG_COMP_SCALE) {
    d.scale = inst->scale - inst->ack_scale;
  }
  if (mask & NG_COMP_LIN_VEL) {
    d.lin_vel[0] = inst->lin_vel[0] - inst->ack_lin_vel[0];
    d.lin_vel[1] = inst->lin_vel[1] - inst->ack_lin_vel[1];
    d.lin_vel[2] = inst->lin_vel[2] - inst->ack_lin_vel[2];
  }
  if (mask & NG_COMP_ANG_VEL) {
    d.ang_vel[0] = inst->ang_vel[0] - inst->ack_ang_vel[0];
    d.ang_vel[1] = inst->ang_vel[1] - inst->ack_ang_vel[1];
    d.ang_vel[2] = inst->ang_vel[2] - inst->ack_ang_vel[2];
  }
  *inout = d;
  return true;
}

float mod_scene_graph_flush_priority(const NgStateUpdate *u) {
  if (!u) {
    return 0.0f;
  }
  float score = 0.0f;
  if (u->comp_mask & NG_COMP_LIN_VEL) {
    score += u->lin_vel[0] * u->lin_vel[0] + u->lin_vel[1] * u->lin_vel[1] +
             u->lin_vel[2] * u->lin_vel[2];
  }
  if (u->comp_mask & NG_COMP_ANG_VEL) {
    score += 0.25f * (u->ang_vel[0] * u->ang_vel[0] + u->ang_vel[1] * u->ang_vel[1] +
                      u->ang_vel[2] * u->ang_vel[2]);
  }
  if (u->comp_mask & (NG_COMP_POS | NG_COMP_ROT)) {
    score += 0.01f;
  }
  return score;
}

int mod_scene_graph_inst_count(void) {
  int n = 0;
  for (int i = 0; i < GGRAPH().inst_count; i++) {
    if (GGRAPH().insts[i].alive) {
      n++;
    }
  }
  return n;
}

const NgSceneInst *mod_scene_graph_inst_at(int index) {
  int n = 0;
  for (int i = 0; i < GGRAPH().inst_count; i++) {
    if (!GGRAPH().insts[i].alive) {
      continue;
    }
    if (n == index) {
      return &GGRAPH().insts[i];
    }
    n++;
  }
  return NULL;
}
// agent: composer-2.5 | 2026-07-29 | instance primary spawn registry | 9c2f5c
// agent: composer-2.5 | 2026-07-29 | entity optional body field | a88872
// agent: composer-2.5 | 2026-07-30 | take dirty apply vel | 22b8f6
// agent: composer-2.5 | 2026-07-29 | stable spawn key order | 091bfd
// agent: composer-2.5 | 2026-07-30 | push state sample ring | 19d0cd
// agent: composer-2.5 | 2026-07-30 | ack baseline encode apply | 1a55f8
// agent: composer-2.5 | 2026-07-30 | fix graph spawn stash leak | cddf4f
