// agent: composer-2.5 | 2026-07-26 | client scene graph store | b7c8d9
#include "mod_scene_graph.h"
#include "vendor/duktape.h"
#include <string.h>

typedef struct ModSceneGraphCtx {
  NgSceneDesc descs[NG_SCENE_DESC_MAX];
  int desc_count;
  NgSceneInst insts[NG_SCENE_INST_MAX];
  int inst_count;
  uint32_t next_local_id;
} ModSceneGraphCtx;

static ModSceneGraphCtx g_graph;

static NgSceneDesc *mod_scene_graph_find_desc(const char *kind, const char *name) {
  for (int i = 0; i < g_graph.desc_count; i++) {
    NgSceneDesc *d = &g_graph.descs[i];
    if (d->alive && strcmp(d->kind, kind) == 0 && strcmp(d->name, name) == 0) {
      return d;
    }
  }
  return NULL;
}

static NgSceneDesc *mod_scene_graph_find_entity_desc(const char *name) {
  return mod_scene_graph_find_desc("entity", name);
}

void mod_scene_graph_reset(void) {
  memset(&g_graph, 0, sizeof(g_graph));
  g_graph.next_local_id = 0x80000000u;
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
  if (g_graph.desc_count >= NG_SCENE_DESC_MAX) {
    return false;
  }
  NgSceneDesc *d = &g_graph.descs[g_graph.desc_count++];
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

bool mod_scene_graph_dispose_desc(const char *kind, const char *name) {
  NgSceneDesc *d = mod_scene_graph_find_desc(kind, name);
  if (!d) {
    return false;
  }
  d->alive = false;
  return true;
}

int mod_scene_graph_spawn(const char *desc_name, uint32_t entity_id, duk_context *ctx,
                          int func_stash_idx) {
  if (!desc_name) {
    return 0;
  }
  NgSceneDesc *d = mod_scene_graph_find_entity_desc(desc_name);
  if (!d) {
    return 0;
  }
  if (g_graph.inst_count >= NG_SCENE_INST_MAX) {
    return 0;
  }
  NgSceneInst *inst = &g_graph.insts[g_graph.inst_count++];
  memset(inst, 0, sizeof(*inst));
  inst->alive = true;
  inst->handle = g_graph.inst_count;
  inst->id = entity_id ? entity_id : g_graph.next_local_id++;
  strncpy(inst->desc_name, desc_name, sizeof(inst->desc_name) - 1);
  strncpy(inst->model, d->model, sizeof(inst->model) - 1);
  inst->sync = d->sync;
  inst->scale = 1.0f;
  inst->script_inst_stash = -1;

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
  for (int i = 0; i < g_graph.inst_count; i++) {
    if (g_graph.insts[i].alive && g_graph.insts[i].handle == handle) {
      return &g_graph.insts[i];
    }
  }
  return NULL;
}

NgSceneInst *mod_scene_graph_inst_by_id(uint32_t entity_id) {
  for (int i = 0; i < g_graph.inst_count; i++) {
    if (g_graph.insts[i].alive && g_graph.insts[i].id == entity_id) {
      return &g_graph.insts[i];
    }
  }
  return NULL;
}

NgSyncMode mod_scene_graph_sync_for_entity(uint32_t entity_id) {
  NgSceneInst *inst = mod_scene_graph_inst_by_id(entity_id);
  if (inst) {
    return inst->sync;
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
  for (int i = 0; i < g_graph.inst_count; i++) {
    NgSceneInst *inst = &g_graph.insts[i];
    if (!inst->alive || inst->comp_dirty == 0) {
      continue;
    }
    if (!ng_sync_posts_wire(inst->sync)) {
      inst->comp_dirty = 0;
      continue;
    }
    if (!(inst->comp_dirty & NG_COMP_ROT)) {
      inst->comp_dirty = 0;
      continue;
    }
    out->entity_id = inst->id;
    out->comp_mask = NG_COMP_ROT;
    out->rot_y = inst->rot[1];
    out->tick = 0;
    inst->comp_dirty &= ~NG_COMP_ROT;
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
  if (update->comp_mask & NG_COMP_ROT) {
    inst->rot[1] = update->rot_y;
  }
}

int mod_scene_graph_inst_count(void) {
  int n = 0;
  for (int i = 0; i < g_graph.inst_count; i++) {
    if (g_graph.insts[i].alive) {
      n++;
    }
  }
  return n;
}

const NgSceneInst *mod_scene_graph_inst_at(int index) {
  int n = 0;
  for (int i = 0; i < g_graph.inst_count; i++) {
    if (!g_graph.insts[i].alive) {
      continue;
    }
    if (n == index) {
      return &g_graph.insts[i];
    }
    n++;
  }
  return NULL;
}

bool mod_scene_graph_has_client_entities(void) {
  for (int i = 0; i < g_graph.inst_count; i++) {
    if (g_graph.insts[i].alive && ng_sync_runs_on_client(g_graph.insts[i].sync)) {
      return true;
    }
  }
  for (int i = 0; i < g_graph.desc_count; i++) {
    if (g_graph.descs[i].alive && strcmp(g_graph.descs[i].kind, "entity") == 0 &&
        ng_sync_runs_on_client(g_graph.descs[i].sync)) {
      return true;
    }
  }
  return false;
}
