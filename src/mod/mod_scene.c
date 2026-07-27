// agent: composer-2.5 | 2026-07-27 | js scene lifecycle host | f3a4b5
#include "mod_scene.h"
#include "core/ng_fs.h"
#include "core/ng_log.h"
#include "core/ng_session.h"
#include "core/ng_sync.h"
#include "mod/mod_input.h"
#include "mod/mod_scene_graph.h"
#include "mod/mod_net.h"
#if !defined(NG_SERVER)
#include "mod/mod_render.h"
#endif
#include "ng_path.h"
#include "vendor/duktape.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define NG_SCENE_STASH_KEY "ng_scene"

typedef struct ModSceneCtx {
  duk_context *ctx;
  char scene_id[32];
  bool is_controller;
  bool is_server_host;
  bool loaded;
  bool inited;
  bool started;
  int scene_inst_stash;
} ModSceneCtx;

static ModSceneCtx g_scene_ctx;

#if defined(NG_SERVER)
void mod_render_apply_session(const NgSessionState *session) { (void)session; }
#endif

static bool mod_scene_is_server(void) {
#ifdef NG_SERVER
  return true;
#else
  return g_scene_ctx.is_server_host;
#endif
}

static ModSceneCtx *mod_scene_from_ctx(duk_context *ctx) {
  (void)ctx;
  return &g_scene_ctx;
}

static NgSceneInst *mod_scene_inst_from_handle(int handle) {
  return mod_scene_graph_inst_by_handle(handle);
}

static int mod_scene_stash_func(duk_context *ctx, const char *name) {
  duk_push_global_stash(ctx);
  char key[48];
  snprintf(key, sizeof(key), "func_%s", name);
  duk_get_prop_string(ctx, -1, key);
  const int idx = duk_normalize_index(ctx, -1);
  duk_pop(ctx);
  return duk_is_function(ctx, idx) ? idx : -1;
}

static void mod_scene_stash_put_func(duk_context *ctx, const char *name) {
  duk_push_global_stash(ctx);
  char key[48];
  snprintf(key, sizeof(key), "func_%s", name);
  duk_dup(ctx, -2);
  duk_put_prop_string(ctx, -2, key);
  duk_pop(ctx);
}

static bool mod_scene_spawn_creates_local(NgSyncMode sync, bool on_server, bool is_controller) {
  if (sync == NG_SYNC_SERVER) {
    return on_server;
  }
  if (sync == NG_SYNC_SHARED || sync == NG_SYNC_LOCAL) {
    return !on_server;
  }
  if (sync == NG_SYNC_OWNER) {
    return !on_server && is_controller;
  }
  return false;
}

static duk_ret_t bind_describe(duk_context *ctx) {
  const char *kind = duk_require_string(ctx, 0);
  const char *name = duk_require_string(ctx, 1);
  NgSyncMode sync = NG_SYNC_SERVER;
  const char *model = NULL;
  int func_idx = -1;

  if (duk_is_object(ctx, 2)) {
    duk_get_prop_string(ctx, 2, "sync");
    if (duk_is_string(ctx, -1)) {
      sync = ng_sync_mode_parse(duk_get_string(ctx, -1));
    }
    duk_pop(ctx);
    duk_get_prop_string(ctx, 2, "mode");
    if (duk_is_string(ctx, -1)) {
      sync = ng_sync_mode_parse(duk_get_string(ctx, -1));
    }
    duk_pop(ctx);
    duk_get_prop_string(ctx, 2, "model");
    if (duk_is_string(ctx, -1)) {
      model = duk_get_string(ctx, -1);
    }
    duk_pop(ctx);
    duk_get_prop_string(ctx, 2, "func");
    if (duk_is_function(ctx, -1)) {
      mod_scene_stash_put_func(ctx, name);
      func_idx = mod_scene_stash_func(ctx, name);
    }
    duk_pop(ctx);
  }

  if (strcmp(kind, "entity") == 0 && !model) {
    model = "cube";
  }
  mod_scene_graph_describe(kind, name, sync, model, func_idx);
  duk_push_int(ctx, 1);
  return 1;
}

static bool mod_scene_call_entity_method(ModSceneCtx *ctx, NgSceneInst *inst, const char *method) {
  if (!ctx->ctx || !inst) {
    return false;
  }
  char key[48];
  snprintf(key, sizeof(key), "inst_%d", inst->handle);
  duk_push_global_stash(ctx->ctx);
  duk_get_prop_string(ctx->ctx, -1, key);
  if (!duk_is_object(ctx->ctx, -1)) {
    duk_pop_n(ctx->ctx, 2);
    return false;
  }
  duk_get_prop_string(ctx->ctx, -1, method);
  if (!duk_is_function(ctx->ctx, -1)) {
    duk_pop_n(ctx->ctx, 3);
    return false;
  }
  duk_insert(ctx->ctx, -2);
  if (duk_pcall_method(ctx->ctx, 0) != DUK_EXEC_SUCCESS) {
    NG_LOG_ERROR("entity.%s: %s", method, duk_safe_to_string(ctx->ctx, -1));
    duk_pop(ctx->ctx);
    return false;
  }
  duk_pop(ctx->ctx);
  return true;
}

static int mod_scene_finish_spawn(duk_context *ctx, ModSceneCtx *scene, const char *name,
                                  uint32_t entity_id, int func_idx) {
  const int handle = mod_scene_graph_spawn(name, entity_id, ctx, func_idx);
  NgSceneInst *inst = mod_scene_inst_from_handle(handle);
  if (inst) {
    mod_scene_call_entity_method(scene, inst, "init");
    mod_scene_call_entity_method(scene, inst, "start");
  }
  return handle;
}

static duk_ret_t bind_spawn(duk_context *ctx) {
  ModSceneCtx *scene = mod_scene_from_ctx(ctx);
  const char *name = duk_require_string(ctx, 0);
  NgSceneDesc *desc = mod_scene_graph_entity_desc(name);
  if (!desc) {
    duk_push_int(ctx, 0);
    return 1;
  }

  NgSceneInst *existing = mod_scene_graph_inst_by_desc(name);
  if (existing) {
    duk_push_int(ctx, existing->handle);
    return 1;
  }

  const bool on_server = mod_scene_is_server();
  const NgSyncMode sync = desc->sync;
  const int func_idx = mod_scene_stash_func(ctx, name);
  uint32_t entity_id = mod_scene_graph_registry_id_for_desc(name);

  if (!mod_scene_spawn_creates_local(sync, on_server, scene->is_controller)) {
    if (entity_id == 0) {
      mod_scene_graph_registry_ensure(name, sync, &entity_id);
    }
    if (!on_server && sync == NG_SYNC_SERVER) {
      const int handle = mod_scene_finish_spawn(ctx, scene, name, entity_id, func_idx);
      duk_push_int(ctx, handle);
      return 1;
    }
    duk_push_int(ctx, 0);
    return 1;
  }

  if (entity_id == 0) {
    mod_scene_graph_registry_ensure(name, sync, &entity_id);
  }

  const int handle = mod_scene_finish_spawn(ctx, scene, name, entity_id, func_idx);
  duk_push_int(ctx, handle);
  return 1;
}

static duk_ret_t bind_despawn(duk_context *ctx) {
  ModSceneCtx *scene = mod_scene_from_ctx(ctx);
  const int handle = duk_require_int(ctx, 0);
  NgSceneInst *inst = mod_scene_inst_from_handle(handle);
  if (inst) {
    mod_scene_call_entity_method(scene, inst, "stop");
    mod_scene_call_entity_method(scene, inst, "dispose");
  }
  mod_scene_graph_despawn(handle);
  return 0;
}

static duk_ret_t bind_dispose(duk_context *ctx) {
  const char *kind = duk_require_string(ctx, 0);
  const char *name = duk_require_string(ctx, 1);
  mod_scene_graph_dispose_desc(kind, name);
  return 0;
}

static duk_ret_t bind_get_input(duk_context *ctx) {
  const int key = duk_require_int(ctx, 0);
  if (key == NG_SCENE_KEY_A) {
    duk_push_boolean(ctx, (mod_input_buttons() & NG_INPUT_A) != 0);
  } else if (key == NG_SCENE_KEY_D) {
    duk_push_boolean(ctx, (mod_input_buttons() & NG_INPUT_D) != 0);
  } else {
    duk_push_false(ctx);
  }
  return 1;
}

static duk_ret_t bind_set_position(duk_context *ctx) {
  NgSceneInst *inst = mod_scene_inst_from_handle(duk_require_int(ctx, 0));
  if (!inst) {
    return 0;
  }
  if (duk_is_object(ctx, 1)) {
    duk_get_prop_string(ctx, 1, "x");
    inst->pos[0] = (float)duk_get_number(ctx, -1);
    duk_pop(ctx);
    duk_get_prop_string(ctx, 1, "y");
    inst->pos[1] = (float)duk_get_number(ctx, -1);
    duk_pop(ctx);
    duk_get_prop_string(ctx, 1, "z");
    inst->pos[2] = (float)duk_get_number(ctx, -1);
    duk_pop(ctx);
  }
  if (ng_sync_posts_wire(inst->sync)) {
    mod_scene_graph_mark_dirty(inst, NG_COMP_POS);
  }
  return 0;
}

static duk_ret_t bind_set_rotation(duk_context *ctx) {
  NgSceneInst *inst = mod_scene_inst_from_handle(duk_require_int(ctx, 0));
  if (!inst) {
    return 0;
  }
  if (duk_is_object(ctx, 1)) {
    duk_get_prop_string(ctx, 1, "x");
    inst->rot[0] = (float)duk_get_number(ctx, -1);
    duk_pop(ctx);
    duk_get_prop_string(ctx, 1, "y");
    inst->rot[1] = (float)duk_get_number(ctx, -1);
    duk_pop(ctx);
    duk_get_prop_string(ctx, 1, "z");
    inst->rot[2] = (float)duk_get_number(ctx, -1);
    duk_pop(ctx);
  } else {
    inst->rot[0] = (float)duk_get_number(ctx, 1);
    inst->rot[1] = (float)duk_get_number(ctx, 2);
    inst->rot[2] = (float)duk_get_number(ctx, 3);
  }
  mod_scene_graph_mark_dirty(inst, NG_COMP_ROT);
  return 0;
}

static duk_ret_t bind_set_rotation_y(duk_context *ctx) {
  NgSceneInst *inst = mod_scene_inst_from_handle(duk_require_int(ctx, 0));
  if (!inst) {
    return 0;
  }
  inst->rot[1] = (float)duk_get_number(ctx, 1);
  mod_scene_graph_mark_dirty(inst, NG_COMP_ROT);
  return 0;
}

static duk_ret_t bind_get_rotation_y(duk_context *ctx) {
  NgSceneInst *inst = mod_scene_inst_from_handle(duk_require_int(ctx, 0));
  if (!inst) {
    duk_push_number(ctx, 0);
    return 1;
  }
  duk_push_number(ctx, inst->rot[1]);
  return 1;
}

static duk_ret_t bind_get_rotation(duk_context *ctx) {
  NgSceneInst *inst = mod_scene_inst_from_handle(duk_require_int(ctx, 0));
  duk_push_object(ctx);
  if (!inst) {
    return 1;
  }
  duk_push_number(ctx, inst->rot[0]);
  duk_put_prop_string(ctx, -2, "x");
  duk_push_number(ctx, inst->rot[1]);
  duk_put_prop_string(ctx, -2, "y");
  duk_push_number(ctx, inst->rot[2]);
  duk_put_prop_string(ctx, -2, "z");
  return 1;
}

static duk_ret_t bind_set_rotation_x(duk_context *ctx) {
  NgSceneInst *inst = mod_scene_inst_from_handle(duk_require_int(ctx, 0));
  if (!inst) {
    return 0;
  }
  inst->rot[0] = (float)duk_get_number(ctx, 1);
  mod_scene_graph_mark_dirty(inst, NG_COMP_ROT);
  return 0;
}

static duk_ret_t bind_get_rotation_x(duk_context *ctx) {
  NgSceneInst *inst = mod_scene_inst_from_handle(duk_require_int(ctx, 0));
  if (!inst) {
    duk_push_number(ctx, 0);
    return 1;
  }
  duk_push_number(ctx, inst->rot[0]);
  return 1;
}

static duk_ret_t bind_set_scale(duk_context *ctx) {
  NgSceneInst *inst = mod_scene_inst_from_handle(duk_require_int(ctx, 0));
  if (!inst) {
    return 0;
  }
  inst->scale = (float)duk_get_number(ctx, 1);
  if (ng_sync_posts_wire(inst->sync)) {
    mod_scene_graph_mark_dirty(inst, NG_COMP_SCALE);
  }
  return 0;
}

static void mod_scene_bind_global(duk_context *ctx) {
  duk_push_global_object(ctx);

#define BIND(name, fn, nargs)                                                                                          \
  duk_push_c_function(ctx, fn, nargs);                                                                                 \
  duk_put_prop_string(ctx, -2, name);

  BIND("describe", bind_describe, 3);
  BIND("spawn", bind_spawn, 2);
  BIND("despawn", bind_despawn, 1);
  BIND("dispose", bind_dispose, 2);
  BIND("get_input", bind_get_input, 1);
  BIND("set_position", bind_set_position, 2);
  BIND("set_rotation", bind_set_rotation, DUK_VARARGS);
  BIND("set_rotation_y", bind_set_rotation_y, 2);
  BIND("set_rotation_x", bind_set_rotation_x, 2);
  BIND("get_rotation_y", bind_get_rotation_y, 1);
  BIND("get_rotation_x", bind_get_rotation_x, 1);
  BIND("get_rotation", bind_get_rotation, 1);
  BIND("set_scale", bind_set_scale, 2);

#undef BIND

  duk_push_int(ctx, NG_SCENE_KEY_A);
  duk_put_prop_string(ctx, -2, "KEY_A");
  duk_push_int(ctx, NG_SCENE_KEY_D);
  duk_put_prop_string(ctx, -2, "KEY_D");

  duk_pop(ctx);
}

static bool mod_scene_load_js(ModSceneCtx *ctx, const char *scene_id) {
  if (!ctx->ctx || !scene_id) {
    return false;
  }
  char path[128];
  snprintf(path, sizeof(path), NG_RES_ROOT "scenes/%s.js", scene_id);
  char *text = ng_fs_read_text(path);
  if (!text) {
    NG_LOG_ERROR("scene js missing: %s", path);
    return false;
  }
  duk_push_string(ctx->ctx, text);
  const bool ok = (duk_peval(ctx->ctx) == DUK_EXEC_SUCCESS);
  if (!ok) {
    NG_LOG_ERROR("scene js error: %s", duk_safe_to_string(ctx->ctx, -1));
  }
  duk_pop(ctx->ctx);
  ng_fs_free_text(text);
  if (!ok) {
    return false;
  }

  mod_scene_bind_global(ctx->ctx);

  duk_push_global_object(ctx->ctx);
  duk_dup(ctx->ctx, -1);
  duk_put_global_string(ctx->ctx, "global");

  duk_get_global_string(ctx->ctx, "Scene");
  if (!duk_is_function(ctx->ctx, -1)) {
    NG_LOG_ERROR("Scene constructor missing");
    duk_pop(ctx->ctx);
    return false;
  }
  duk_new(ctx->ctx, 0);
  if (!duk_is_object(ctx->ctx, -1)) {
    NG_LOG_ERROR("Scene instance create failed");
    duk_pop(ctx->ctx);
    return false;
  }
  duk_push_global_stash(ctx->ctx);
  duk_insert(ctx->ctx, -2);
  duk_put_prop_string(ctx->ctx, -2, NG_SCENE_STASH_KEY);
  duk_pop(ctx->ctx);
  ctx->scene_inst_stash = 1;
  return true;
}

static bool mod_scene_call_method(ModSceneCtx *ctx, const char *method, int nargs) {
  if (!ctx->ctx) {
    return false;
  }
  duk_push_global_stash(ctx->ctx);
  duk_get_prop_string(ctx->ctx, -1, NG_SCENE_STASH_KEY);
  duk_remove(ctx->ctx, -2);
  if (!duk_is_object(ctx->ctx, -1)) {
    duk_pop(ctx->ctx);
    return false;
  }
  duk_get_prop_string(ctx->ctx, -1, method);
  if (!duk_is_function(ctx->ctx, -1)) {
    duk_pop_n(ctx->ctx, 2);
    return false;
  }
  if (nargs > 0) {
    duk_insert(ctx->ctx, -(nargs + 2));
    duk_insert(ctx->ctx, -(nargs + 1));
  } else {
    duk_insert(ctx->ctx, -2);
  }
  if (duk_pcall_method(ctx->ctx, nargs) != DUK_EXEC_SUCCESS) {
    NG_LOG_ERROR("scene.%s: %s", method, duk_safe_to_string(ctx->ctx, -1));
    duk_pop(ctx->ctx);
    return false;
  }
  duk_pop(ctx->ctx);
  return true;
}

static void mod_scene_call_lifecycle(ModSceneCtx *ctx, const char *method) {
  mod_scene_call_method(ctx, method, 0);
}

static void mod_scene_push_session_obj(ModSceneCtx *ctx, const NgSessionState *session) {
  duk_push_object(ctx->ctx);
  duk_push_string(ctx->ctx, session->scene_id);
  duk_put_prop_string(ctx->ctx, -2, "scene_id");
  duk_push_boolean(ctx->ctx, ctx->is_controller ? 1 : 0);
  duk_put_prop_string(ctx->ctx, -2, "is_controller");
  duk_push_int(ctx->ctx, session->controller_id);
  duk_put_prop_string(ctx->ctx, -2, "controller_id");
  duk_push_int(ctx->ctx, session->your_id);
  duk_put_prop_string(ctx->ctx, -2, "your_id");
}

static int mod_scene_route_registry_spawn(ModSceneCtx *ctx, const NgSessionSpawn *sp) {
  if (mod_scene_graph_inst_by_desc(sp->desc_name)) {
    return 0;
  }
  mod_scene_graph_registry_add(sp->desc_name, sp->entity_id, sp->sync);
  const bool on_server = mod_scene_is_server();
  if (!mod_scene_spawn_creates_local(sp->sync, on_server, ctx->is_controller)) {
    if (sp->sync == NG_SYNC_SERVER && !on_server) {
      const int func_idx = mod_scene_stash_func(ctx->ctx, sp->desc_name);
      return mod_scene_finish_spawn(ctx->ctx, ctx, sp->desc_name, sp->entity_id, func_idx);
    }
    return 0;
  }
  const int func_idx = mod_scene_stash_func(ctx->ctx, sp->desc_name);
  return mod_scene_finish_spawn(ctx->ctx, ctx, sp->desc_name, sp->entity_id, func_idx);
}

static void mod_scene_run_entity_steps(ModSceneCtx *ctx, float dt) {
  if (!ctx->ctx) {
    return;
  }
  const bool on_server = mod_scene_is_server();
  const int n = mod_scene_graph_inst_count();
  for (int i = 0; i < n; i++) {
    const NgSceneInst *inst = mod_scene_graph_inst_at(i);
    if (!inst) {
      continue;
    }
    if (on_server) {
      if (inst->sync != NG_SYNC_SERVER) {
        continue;
      }
    } else {
      if (inst->sync == NG_SYNC_SERVER) {
        continue;
      }
      if (inst->sync == NG_SYNC_OWNER && !ctx->is_controller) {
        continue;
      }
    }
    char key[48];
    snprintf(key, sizeof(key), "inst_%d", inst->handle);
    duk_push_global_stash(ctx->ctx);
    duk_get_prop_string(ctx->ctx, -1, key);
    if (!duk_is_object(ctx->ctx, -1)) {
      duk_pop_n(ctx->ctx, 2);
      continue;
    }
    duk_get_prop_string(ctx->ctx, -1, "step");
    if (!duk_is_function(ctx->ctx, -1)) {
      duk_pop_n(ctx->ctx, 3);
      continue;
    }
    duk_insert(ctx->ctx, -2);
    duk_push_number(ctx->ctx, dt);
    if (duk_pcall_method(ctx->ctx, 1) != DUK_EXEC_SUCCESS) {
      NG_LOG_ERROR("entity step: %s", duk_safe_to_string(ctx->ctx, -1));
    }
    duk_pop(ctx->ctx);
  }
}

static void mod_scene_unload(ModSceneCtx *ctx) {
  if (ctx->ctx && ctx->started) {
    mod_scene_call_lifecycle(ctx, "stop");
    mod_scene_call_lifecycle(ctx, "dispose");
  }
  if (ctx->ctx) {
    duk_destroy_heap(ctx->ctx);
    ctx->ctx = NULL;
  }
  mod_scene_graph_reset();
  ctx->loaded = false;
  ctx->inited = false;
  ctx->started = false;
  ctx->is_controller = false;
  ctx->is_server_host = false;
  ctx->scene_id[0] = '\0';
}

static bool mod_scene_begin(const char *scene_id, bool server_host, bool is_controller) {
  ModSceneCtx *ctx = &g_scene_ctx;
  mod_scene_unload(ctx);
  ctx->ctx = duk_create_heap_default();
  if (!ctx->ctx) {
    return false;
  }
  strncpy(ctx->scene_id, scene_id, sizeof(ctx->scene_id) - 1);
  ctx->is_server_host = server_host;
  ctx->is_controller = is_controller;
  mod_scene_graph_reset();
  if (!mod_scene_load_js(ctx, scene_id)) {
    mod_scene_unload(ctx);
    return false;
  }
  ctx->loaded = true;
  mod_scene_call_lifecycle(ctx, "init");
  ctx->inited = true;
  return true;
}

bool mod_scene_load(const char *scene_id) {
  if (!scene_id || scene_id[0] == '\0') {
    mod_scene_unload(&g_scene_ctx);
    return true;
  }
  ModSceneCtx *ctx = &g_scene_ctx;
  if (!mod_scene_begin(scene_id, true, true)) {
    return false;
  }
  NgSessionState session = {0};
  strncpy(session.scene_id, scene_id, sizeof(session.scene_id) - 1);
  mod_scene_push_session_obj(ctx, &session);
  if (!mod_scene_call_method(ctx, "start", 1)) {
    mod_scene_unload(ctx);
    return false;
  }
  ctx->started = true;
  return true;
}

bool mod_scene_is_loaded(void) { return g_scene_ctx.loaded; }

const char *mod_scene_current_id(void) { return g_scene_ctx.scene_id; }

int mod_scene_entity_count(void) { return mod_scene_graph_inst_count(); }

void mod_scene_fill_session(NgSessionState *session) {
  if (!session) {
    return;
  }
  mod_scene_graph_fill_session_spawns(session);
}

bool mod_scene_is_controller(void) { return g_scene_ctx.is_controller; }

bool mod_scene_can_author(NgSyncMode sync) {
  if (sync == NG_SYNC_SHARED) {
    return true;
  }
  if (sync == NG_SYNC_OWNER) {
    return g_scene_ctx.is_controller;
  }
  return false;
}

void mod_scene_on_session(const NgSessionState *session) {
  ModSceneCtx *ctx = &g_scene_ctx;
  if (!session) {
    return;
  }

  if (!ctx->ctx || strcmp(ctx->scene_id, session->scene_id) != 0) {
    if (!mod_scene_begin(session->scene_id, false,
                         session->your_id != 0 && session->your_id == session->controller_id)) {
      return;
    }
  } else {
    ctx->is_controller =
        (session->your_id != 0 && session->your_id == session->controller_id);
  }

  for (int i = 0; i < session->spawn_count; i++) {
    mod_scene_route_registry_spawn(ctx, &session->spawns[i]);
  }

  if (!ctx->started) {
    mod_scene_push_session_obj(ctx, session);
    mod_scene_call_method(ctx, "start", 1);
    ctx->started = true;
  }

#ifndef NG_SERVER
  mod_render_apply_session(session);
#endif
}

void mod_scene_apply_remote(const NgStateUpdate *update) {
  if (!update || !g_scene_ctx.loaded) {
    return;
  }
  mod_scene_graph_apply_update(update);
}

bool mod_scene_take_flush(NgStateUpdate *out) {
  if (!out || !g_scene_ctx.loaded) {
    return false;
  }
  NgStateUpdate tmp;
  while (mod_scene_graph_take_dirty(&tmp)) {
    NgSceneInst *inst = mod_scene_graph_inst_by_id(tmp.entity_id);
    if (inst && mod_scene_can_author(inst->sync)) {
      *out = tmp;
      return true;
    }
    if (inst && inst->sync == NG_SYNC_SERVER && mod_scene_is_server()) {
      *out = tmp;
      return true;
    }
  }
  return false;
}

static bool mod_scene_on_msg(const NgMsg *msg, void *vctx) {
  ModSceneCtx *ctx = (ModSceneCtx *)vctx;
  if (!ctx || !msg) {
    return false;
  }
  if (msg->kind != NG_MSG_TICK || msg->to != NG_BUS_ANY) {
    return false;
  }
  if (!ctx->loaded || !ctx->started || !ctx->ctx) {
    return true;
  }

  mod_input_begin_frame();
  duk_push_number(ctx->ctx, msg->dt);
  mod_scene_call_method(ctx, "step", 1);
  mod_scene_run_entity_steps(ctx, msg->dt);
  mod_net_flush_scene_updates();
  return true;
}

static bool mod_scene_init(void *vctx) {
  (void)vctx;
  memset(&g_scene_ctx, 0, sizeof(g_scene_ctx));
  mod_scene_graph_reset();
  return true;
}

static void mod_scene_shutdown(void *vctx) {
  (void)vctx;
  mod_scene_unload(&g_scene_ctx);
}

static const NgModOps g_scene_ops = {
    .name = "scene",
    .dest = NG_BUS_SCENE,
    .init = mod_scene_init,
    .shutdown = mod_scene_shutdown,
    .on_msg = mod_scene_on_msg,
};

const NgModOps *mod_scene_ops(void) { return &g_scene_ops; }

void *mod_scene_ctx(void) { return &g_scene_ctx; }

bool mod_scene_graph_active(void) { return mod_scene_graph_inst_count() > 0; }

// agent: composer-2.5 | 2026-07-27 | smoke cube registry sphere inst | c5f796
static bool mod_scene_smoke_one(const char *scene_id, const char *spawn_desc, bool expect_inst) {
  if (!mod_scene_begin(scene_id, true, true)) {
    return false;
  }
  ModSceneCtx *ctx = &g_scene_ctx;
  NgSessionState session = {0};
  strncpy(session.scene_id, scene_id, sizeof(session.scene_id) - 1);
  mod_scene_push_session_obj(ctx, &session);
  if (!mod_scene_call_method(ctx, "start", 1)) {
    mod_scene_unload(ctx);
    return false;
  }
  ctx->started = true;
  const bool ok =
      expect_inst ? (mod_scene_graph_inst_count() >= 1)
                  : (mod_scene_graph_registry_id_for_desc(spawn_desc) != 0);
  mod_scene_unload(ctx);
  return ok;
}

bool mod_scene_smoke_test(void) {
  return mod_scene_smoke_one("cube", "cube_a_e", false) &&
         mod_scene_smoke_one("sphere", "sphere_a_e", true);
}
