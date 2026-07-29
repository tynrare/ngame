// agent: composer-2.5 | 2026-07-27 | spawn registry and routing | e2f3a4
// agent: composer-2.5 | 2026-07-29 | graph uses active runtime | 48c6e0
#include "graph.h"
#include "scene/runtime.h"
#include "world/ng_world.h"
#include "vendor/duktape.h"
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
                              const char *model, int func_stash_idx) {
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

static NgSceneRegistry *mod_scene_graph_find_registry(const char *desc_name) {
  for (int i = 0; i < GGRAPH().registry_count; i++) {
    NgSceneRegistry *r = &GGRAPH().registry[i];
    if (r->alive && strcmp(r->desc_name, desc_name) == 0) {
      return r;
    }
  }
  return NULL;
}

bool mod_scene_graph_registry_add(const char *desc_name, uint32_t entity_id, NgSyncMode sync) {
  if (!desc_name || entity_id == 0) {
    return false;
  }
  NgSceneRegistry *existing = mod_scene_graph_find_registry(desc_name);
  if (existing) {
    existing->entity_id = entity_id;
    existing->sync = sync;
    return true;
  }
  if (GGRAPH().registry_count >= NG_SCENE_INST_MAX) {
    return false;
  }
  NgSceneRegistry *r = &GGRAPH().registry[GGRAPH().registry_count++];
  memset(r, 0, sizeof(*r));
  r->alive = true;
  strncpy(r->desc_name, desc_name, sizeof(r->desc_name) - 1);
  r->entity_id = entity_id;
  r->sync = sync;
  return true;
}

uint32_t mod_scene_graph_registry_id_for_desc(const char *desc_name) {
  NgSceneRegistry *r = mod_scene_graph_find_registry(desc_name);
  return r ? r->entity_id : 0;
}

bool mod_scene_graph_registry_ensure(const char *desc_name, NgSyncMode sync, uint32_t *out_id) {
  NgSceneRegistry *r = mod_scene_graph_find_registry(desc_name);
  if (r) {
    if (out_id) {
      *out_id = r->entity_id;
    }
    return true;
  }
  const uint32_t id = mod_scene_graph_alloc_id();
  if (!mod_scene_graph_registry_add(desc_name, id, sync)) {
    return false;
  }
  if (out_id) {
    *out_id = id;
  }
  return true;
}

void mod_scene_graph_fill_session_spawns(NgSessionState *session) {
  if (!session) {
    return;
  }
  session->spawn_count = 0;
  for (int i = 0; i < GGRAPH().registry_count && session->spawn_count < NG_SESSION_SPAWN_MAX; i++) {
    NgSceneRegistry *r = &GGRAPH().registry[i];
    if (!r->alive) {
      continue;
    }
    NgSessionSpawn *sp = &session->spawns[session->spawn_count++];
    sp->entity_id = r->entity_id;
    strncpy(sp->desc_name, r->desc_name, sizeof(sp->desc_name) - 1);
    sp->sync = r->sync;
  }
}

int mod_scene_graph_spawn(const char *desc_name, uint32_t entity_id, duk_context *ctx,
                          int func_stash_idx) {
  if (!desc_name) {
    return 0;
  }
  NgSceneDesc *d = mod_scene_graph_entity_desc(desc_name);
  if (!d) {
    return 0;
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
  strncpy(inst->model, d->model, sizeof(inst->model) - 1);
  inst->sync = d->sync;
  inst->scale = 1.0f;
  inst->script_inst_stash = -1;
  mod_scene_graph_registry_add(desc_name, inst->id, d->sync);

  if (ctx && func_stash_idx >= 0) {
    char func_key[48];
    snprintf(func_key, sizeof(func_key), "func_%s", desc_name);
    duk_push_global_stash(ctx);
    duk_get_prop_string(ctx, -1, func_key);
    if (duk_is_function(ctx, -1)) {
      duk_new(ctx, 0);
      char inst_key[48];
      snprintf(inst_key, sizeof(inst_key), "inst_%d", inst->handle);
      duk_push_global_stash(ctx);
      duk_insert(ctx, -2);
      duk_put_prop_string(ctx, -2, inst_key);
      duk_pop(ctx);
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
  for (int i = 0; i < GGRAPH().registry_count; i++) {
    if (GGRAPH().registry[i].alive && GGRAPH().registry[i].entity_id == entity_id) {
      return GGRAPH().registry[i].sync;
    }
  }
  return NG_SYNC_SERVER;
}

void mod_scene_graph_mark_dirty(NgSceneInst *inst, uint32_t comp) {
  if (inst) {
    inst->comp_dirty |= comp;
  }
}

bool mod_scene_graph_take_dirty(NgStateUpdate *out) {
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
    const uint32_t wire_mask = inst->comp_dirty & (NG_COMP_POS | NG_COMP_ROT | NG_COMP_SCALE);
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
  if (!inst) {
    return;
  }
  if (update->seq == inst->last_sent_seq && inst->last_sent_seq != 0) {
    return;
  }
  if (inst->last_applied_seq != 0 &&
      (uint16_t)(update->seq - inst->last_applied_seq) > 0x8000u) {
    return;
  }
  if (update->seq == inst->last_applied_seq) {
    return;
  }
  inst->last_applied_seq = update->seq;
  if (update->comp_mask & NG_COMP_POS) {
    inst->pos[0] = update->pos[0];
    inst->pos[1] = update->pos[1];
    inst->pos[2] = update->pos[2];
  }
  if (update->comp_mask & NG_COMP_ROT) {
    inst->rot[0] = update->rot[0];
    inst->rot[1] = update->rot[1];
    inst->rot[2] = update->rot[2];
  }
  if (update->comp_mask & NG_COMP_SCALE) {
    inst->scale = update->scale;
  }
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
