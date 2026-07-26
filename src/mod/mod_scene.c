// agent: composer-2.5 | 2026-07-26 | scene lifecycle and global API | e1f2a3
#include "mod_scene.h"
#include "core/ng_fs.h"
#include "core/ng_log.h"
#include "core/ng_session.h"
#include "core/ng_sync.h"
#include "mod/mod_input.h"
#include "mod/mod_render.h"
#include "mod/mod_scene_graph.h"
#if defined(NG_HAS_EMBEDDED) || !defined(NG_SERVER)
#include "mod/mod_net.h"
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
  NgSyncMode scene_sync;
  bool is_controller;
  bool loaded;
  bool inited;
  bool started;
  int scene_inst_stash;
  uint32_t bootstrap_entity_id;
} ModSceneCtx;

static ModSceneCtx g_scene_ctx;

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

static duk_ret_t bind_spawn(duk_context *ctx) {
  const char *name = duk_require_string(ctx, 0);
  uint32_t entity_id = g_scene_ctx.bootstrap_entity_id;
  const int func_idx = mod_scene_stash_func(ctx, name);

  if (duk_is_object(ctx, 1)) {
    duk_get_prop_string(ctx, 1, "entity_id");
    if (duk_is_number(ctx, -1)) {
      entity_id = (uint32_t)duk_get_uint(ctx, -1);
    }
    duk_pop(ctx);
  }

  const int handle = mod_scene_graph_spawn(name, entity_id, ctx, func_idx);
  duk_push_int(ctx, handle);
  return 1;
}

static duk_ret_t bind_despawn(duk_context *ctx) {
  const int handle = duk_require_int(ctx, 0);
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

// agent: composer-2.5 | 2026-07-26 | local-only pos no wire dirty | f3a4b5
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

static duk_ret_t bind_set_scale(duk_context *ctx) {
  NgSceneInst *inst = mod_scene_inst_from_handle(duk_require_int(ctx, 0));
  if (!inst) {
    return 0;
  }
  inst->scale = (float)duk_get_number(ctx, 1);
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
  BIND("get_rotation_y", bind_get_rotation_y, 1);
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
  duk_insert(ctx->ctx, -(nargs + 1));
  duk_get_prop_string(ctx->ctx, -(nargs + 2), method);
  if (!duk_is_function(ctx->ctx, -1)) {
    duk_pop_n(ctx->ctx, nargs + 3);
    return false;
  }
  duk_insert(ctx->ctx, -(nargs + 2));
  if (duk_pcall(ctx->ctx, nargs) != DUK_EXEC_SUCCESS) {
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
  duk_push_uint(ctx->ctx, session->cube_entity_id);
  duk_put_prop_string(ctx->ctx, -2, "entity_id");
  duk_push_string(ctx->ctx, ng_sync_mode_name(session->scene_sync));
  duk_put_prop_string(ctx->ctx, -2, "sync");
  duk_push_boolean(ctx->ctx, ctx->is_controller ? 1 : 0);
  duk_put_prop_string(ctx->ctx, -2, "is_controller");
  duk_push_int(ctx->ctx, session->controller_id);
  duk_put_prop_string(ctx->ctx, -2, "controller_id");
  duk_push_int(ctx->ctx, session->your_id);
  duk_put_prop_string(ctx->ctx, -2, "your_id");
}

static void mod_scene_run_entity_steps(ModSceneCtx *ctx, float dt) {
  if (!ctx->ctx) {
    return;
  }
  const int n = mod_scene_graph_inst_count();
  for (int i = 0; i < n; i++) {
    const NgSceneInst *inst = mod_scene_graph_inst_at(i);
    if (!inst) {
      continue;
    }
    if (inst->sync == NG_SYNC_OWNER && !ctx->is_controller) {
      continue;
    }
    if (inst->sync == NG_SYNC_SERVER) {
      continue;
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
  ctx->bootstrap_entity_id = 0;
  ctx->scene_id[0] = '\0';
}

bool mod_scene_client_fields_active(void) {
  return g_scene_ctx.loaded && g_scene_ctx.scene_sync != NG_SYNC_SERVER;
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

  if (!ng_scene_has_js_host(session->scene_id)) {
    mod_scene_unload(ctx);
    mod_render_apply_session(session);
    return;
  }

  if (!ctx->ctx || strcmp(ctx->scene_id, session->scene_id) != 0) {
    mod_scene_unload(ctx);
    ctx->ctx = duk_create_heap_default();
    if (!ctx->ctx) {
      return;
    }
    strncpy(ctx->scene_id, session->scene_id, sizeof(ctx->scene_id) - 1);
    mod_scene_graph_reset();
    if (!mod_scene_load_js(ctx, session->scene_id)) {
      mod_scene_unload(ctx);
      return;
    }
    ctx->loaded = true;
    ctx->inited = false;
    ctx->started = false;
  }

  ctx->scene_sync = session->scene_sync;
  ctx->is_controller =
      (session->your_id != 0 && session->your_id == session->controller_id);
  ctx->bootstrap_entity_id = session->cube_entity_id;

  if (!ctx->inited) {
    mod_scene_call_lifecycle(ctx, "init");
    ctx->inited = true;
  }
  if (!ctx->started) {
    mod_scene_push_session_obj(ctx, session);
    mod_scene_call_method(ctx, "start", 1);
    ctx->started = true;
  }

  mod_render_apply_session(session);
}

void mod_scene_apply_remote(const NgStateUpdate *update) {
  ModSceneCtx *ctx = &g_scene_ctx;
  if (!update || !ctx->loaded || !ng_scene_has_js_host(ctx->scene_id)) {
    return;
  }
  // agent: composer-2.5 | 2026-07-26 | ignore stale shared rot snap | bc2454
  NgSceneInst *inst = mod_scene_graph_inst_by_id(update->entity_id);
  if (inst && inst->sync == NG_SYNC_SHARED && (update->comp_mask & NG_COMP_ROT) &&
      fabsf(inst->rot[1]) > 0.05f && fabsf(update->rot_y) < 0.05f) {
    return;
  }
  mod_scene_graph_apply_update(update);
}

bool mod_scene_take_flush(NgStateUpdate *out) {
  ModSceneCtx *ctx = &g_scene_ctx;
  if (!out || !ctx->loaded) {
    return false;
  }
  NgStateUpdate tmp;
  while (mod_scene_graph_take_dirty(&tmp)) {
    NgSceneInst *inst = mod_scene_graph_inst_by_id(tmp.entity_id);
    if (inst && mod_scene_can_author(inst->sync)) {
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
  if (!ng_sync_runs_on_client(ctx->scene_sync) && mod_scene_graph_inst_count() == 0) {
    return true;
  }

  mod_input_begin_frame();
  duk_push_number(ctx->ctx, msg->dt);
  mod_scene_call_method(ctx, "step", 1);
  mod_scene_run_entity_steps(ctx, msg->dt);
#if defined(NG_HAS_EMBEDDED) || !defined(NG_SERVER)
  mod_net_flush_scene_updates();
#endif
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
