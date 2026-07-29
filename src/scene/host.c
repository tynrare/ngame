// agent: composer-2.5 | 2026-07-27 | js scene lifecycle host | f3a4b5
// agent: composer-2.5 | 2026-07-29 | host server view split | 1b39ad
#include "scene.h"
#include "scene/runtime.h"
#include "engine/ng_fs.h"
#include "engine/ng_log.h"
#include "engine/ng_session.h"
#include "engine/ng_sync.h"
#include "client/input.h"
#include "scene/graph.h"
#include "scene/assets.h"
#include "scene/native.h"
#include "net/mod_net.h"
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
#include "server/sim.h"
#endif
#include "world/ng_world.h"
#if !defined(NG_SERVER)
#include "client/render.h"
#endif
#include "ng_path.h"
#include "vendor/duktape.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define NG_SCENE_STASH_KEY "ng_scene"
#define NG_SCENE_ACTIVE() (mod_scene_runtime_scene())

#if defined(NG_SERVER)
void mod_render_apply_session(const NgSessionState *session) { (void)session; }
#endif

static bool mod_scene_is_server(void) {
#ifdef NG_SERVER
  return true;
#else
  return NG_SCENE_ACTIVE()->is_server_host;
#endif
}

static ModSceneCtx *mod_scene_from_ctx(duk_context *ctx) {
  (void)ctx;
  return mod_scene_runtime_scene();
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

static float mod_scene_read_opt_number(duk_context *ctx, int obj_idx, const char *key, float fallback) {
  duk_get_prop_string(ctx, obj_idx, key);
  const float v = duk_is_number(ctx, -1) ? (float)duk_get_number(ctx, -1) : fallback;
  duk_pop(ctx);
  return v;
}

static bool mod_scene_read_opt_vec3(duk_context *ctx, int obj_idx, const char *key, float out[3]) {
  duk_get_prop_string(ctx, obj_idx, key);
  if (!duk_is_object(ctx, -1)) {
    duk_pop(ctx);
    return false;
  }
  const int vidx = duk_get_top_index(ctx);
  out[0] = mod_scene_read_opt_number(ctx, vidx, "x", 0.0f);
  out[1] = mod_scene_read_opt_number(ctx, vidx, "y", 0.0f);
  out[2] = mod_scene_read_opt_number(ctx, vidx, "z", 0.0f);
  duk_pop(ctx);
  return true;
}

static bool mod_scene_parse_view_describe(duk_context *ctx, int obj_idx) {
  NgSceneViewMeta view = {0};
  view.valid = true;
  view.bg_r = 0;
  view.bg_g = 0;
  view.bg_b = 0;
  view.camera_mode = NG_SCENE_CAM_FIXED;
  view.cam_fovy = 45.0f;
  view.orbit_radius = 6.0f;
  view.orbit_speed = 0.6f;
  view.orbit_height = 2.0f;
  view.cam_pos[2] = 6.0f;
  view.cam_target[0] = view.cam_target[1] = view.cam_target[2] = 0.0f;

  duk_get_prop_string(ctx, obj_idx, "bg");
  if (duk_is_object(ctx, -1)) {
    const int bg_idx = duk_get_top_index(ctx);
    view.bg_r = (uint8_t)mod_scene_read_opt_number(ctx, bg_idx, "r", 0.0f);
    view.bg_g = (uint8_t)mod_scene_read_opt_number(ctx, bg_idx, "g", 0.0f);
    view.bg_b = (uint8_t)mod_scene_read_opt_number(ctx, bg_idx, "b", 0.0f);
  }
  duk_pop(ctx);

  duk_get_prop_string(ctx, obj_idx, "camera");
  if (duk_is_object(ctx, -1)) {
    const int cam_idx = duk_get_top_index(ctx);
    duk_get_prop_string(ctx, cam_idx, "mode");
    if (duk_is_string(ctx, -1) && strcmp(duk_get_string(ctx, -1), "orbit") == 0) {
      view.camera_mode = NG_SCENE_CAM_ORBIT;
    }
    duk_pop(ctx);
    (void)mod_scene_read_opt_vec3(ctx, cam_idx, "position", view.cam_pos);
    (void)mod_scene_read_opt_vec3(ctx, cam_idx, "target", view.cam_target);
    view.cam_fovy = mod_scene_read_opt_number(ctx, cam_idx, "fovy", view.cam_fovy);
    duk_get_prop_string(ctx, cam_idx, "orbit");
    if (duk_is_object(ctx, -1)) {
      const int orb_idx = duk_get_top_index(ctx);
      view.orbit_radius = mod_scene_read_opt_number(ctx, orb_idx, "radius", view.orbit_radius);
      view.orbit_speed = mod_scene_read_opt_number(ctx, orb_idx, "speed", view.orbit_speed);
      view.orbit_height = mod_scene_read_opt_number(ctx, orb_idx, "height", view.orbit_height);
    }
    duk_pop(ctx);
  }
  duk_pop(ctx);
  return mod_scene_assets_describe_view(&view);
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
    if (strcmp(kind, "mesh") == 0) {
      float w = 1.0f, h = 1.0f, d = 1.0f;
      const char *shape = "cube";
      duk_get_prop_string(ctx, 2, "width");
      if (duk_is_number(ctx, -1)) {
        w = (float)duk_get_number(ctx, -1);
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "height");
      if (duk_is_number(ctx, -1)) {
        h = (float)duk_get_number(ctx, -1);
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "depth");
      if (duk_is_number(ctx, -1)) {
        d = (float)duk_get_number(ctx, -1);
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "shape");
      if (duk_is_string(ctx, -1)) {
        shape = duk_get_string(ctx, -1);
      }
      duk_pop(ctx);
      // agent: composer-2.5 | 2026-07-28 | wire js shape into mesh describe | f1a2b3
      mod_scene_assets_describe_mesh(name, shape, w, h, d);
    } else if (strcmp(kind, "shader") == 0) {
      const char *fragment = NULL;
      const char *vertex = NULL;
      uint8_t tr = 255, tg = 255, tb = 255;
      bool have_tint = false;
      duk_get_prop_string(ctx, 2, "fragment");
      if (duk_is_string(ctx, -1)) {
        fragment = duk_get_string(ctx, -1);
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "vertex");
      if (duk_is_string(ctx, -1)) {
        vertex = duk_get_string(ctx, -1);
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "tint");
      if (duk_is_object(ctx, -1)) {
        const int tidx = duk_get_top_index(ctx);
        tr = (uint8_t)mod_scene_read_opt_number(ctx, tidx, "r", 255.0f);
        tg = (uint8_t)mod_scene_read_opt_number(ctx, tidx, "g", 255.0f);
        tb = (uint8_t)mod_scene_read_opt_number(ctx, tidx, "b", 255.0f);
        have_tint = true;
      }
      duk_pop(ctx);
      mod_scene_assets_describe_shader(name, fragment, vertex, tr, tg, tb, have_tint);
    } else if (strcmp(kind, "model") == 0) {
      const char *mesh = NULL;
      const char *shader = NULL;
      duk_get_prop_string(ctx, 2, "mesh");
      if (duk_is_string(ctx, -1)) {
        mesh = duk_get_string(ctx, -1);
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "shader");
      if (duk_is_string(ctx, -1)) {
        shader = duk_get_string(ctx, -1);
      }
      duk_pop(ctx);
      mod_scene_assets_describe_model(name, mesh, shader);
    } else if (strcmp(kind, "entity") == 0) {
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
    } else if (strcmp(kind, "scene") == 0) {
      // agent: composer-2.5 | 2026-07-28 | js scene view bg camera meta | e2f3a4
      mod_scene_parse_view_describe(ctx, 2);
    }
  }

  if (strcmp(kind, "entity") == 0) {
    if (!model) {
      duk_push_int(ctx, 0);
      return 1;
    }
    mod_scene_graph_describe(kind, name, sync, model, func_idx);
  } else if (strcmp(kind, "mesh") == 0 || strcmp(kind, "shader") == 0 ||
             strcmp(kind, "model") == 0 || strcmp(kind, "scene") == 0) {
    mod_scene_graph_describe(kind, name, sync, model, func_idx);
  }
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
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
    if (inst->world_id != 0) {
      ng_world_despawn(mod_sim_world(), inst->world_id);
      inst->world_id = 0;
    }
#endif
    mod_scene_call_entity_method(scene, inst, "stop");
    mod_scene_call_entity_method(scene, inst, "dispose");
  }
  mod_scene_graph_despawn(handle);
  return 0;
}

static duk_ret_t bind_dispose(duk_context *ctx) {
  const char *kind = duk_require_string(ctx, 0);
  const char *name = duk_require_string(ctx, 1);
  mod_scene_assets_dispose(kind, name);
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

// agent: composer-2.5 | 2026-07-29 | js change scene binding and drain | d70ea8
static duk_ret_t bind_is_server(duk_context *ctx) {
  duk_push_boolean(ctx, mod_scene_is_server() ? 1 : 0);
  return 1;
}

static duk_ret_t bind_change_scene(duk_context *ctx) {
  ModSceneCtx *scene = mod_scene_from_ctx(ctx);
  const char *scene_id = duk_require_string(ctx, 0);
  if (!scene || !scene_id || scene_id[0] == '\0' || !mod_scene_is_server()) {
    duk_push_false(ctx);
    return 1;
  }
  strncpy(scene->pending_scene_id, scene_id, sizeof(scene->pending_scene_id) - 1);
  scene->pending_scene_id[sizeof(scene->pending_scene_id) - 1] = '\0';
  duk_push_true(ctx);
  return 1;
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
  BIND("is_server", bind_is_server, 0);
  BIND("change_scene", bind_change_scene, 1);

#undef BIND

  duk_push_int(ctx, NG_SCENE_KEY_A);
  duk_put_prop_string(ctx, -2, "KEY_A");
  duk_push_int(ctx, NG_SCENE_KEY_D);
  duk_put_prop_string(ctx, -2, "KEY_D");

  duk_pop(ctx);
}

static bool mod_scene_eval_js_file(duk_context *ctx, const char *path) {
  if (!ctx || !path) {
    return false;
  }
  char *text = ng_fs_read_text(path);
  if (!text) {
    return false;
  }
  duk_push_string(ctx, text);
  const bool ok = (duk_peval(ctx) == DUK_EXEC_SUCCESS);
  if (!ok) {
    NG_LOG_ERROR("scene js error (%s): %s", path, duk_safe_to_string(ctx, -1));
  }
  duk_pop(ctx);
  ng_fs_free_text(text);
  return ok;
}

static bool mod_scene_load_helpers(duk_context *ctx) {
  char path[128];
  snprintf(path, sizeof(path), NG_RES_ROOT "helpers.js");
  if (!mod_scene_eval_js_file(ctx, path)) {
    NG_LOG_WARN("helpers.js missing: %s", path);
    return false;
  }
  return true;
}

static bool mod_scene_wants_helpers(const char *scene_id) {
  return scene_id && strcmp(scene_id, "example") == 0;
}

static bool mod_scene_finish_js_load(ModSceneCtx *ctx) {
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

static bool mod_scene_load_js_path(ModSceneCtx *ctx, const char *path, bool with_helpers) {
  if (with_helpers && !mod_scene_load_helpers(ctx->ctx)) {
    return false;
  }
  if (!mod_scene_eval_js_file(ctx->ctx, path)) {
    return false;
  }
  return mod_scene_finish_js_load(ctx);
}

static bool mod_scene_load_js(ModSceneCtx *ctx, const char *scene_id) {
  if (!ctx->ctx || !scene_id) {
    return false;
  }
  char path[128];
  if (strcmp(scene_id, "boot") == 0) {
    // agent: composer-2.5 | 2026-07-28 | boot js outside scenes dir | b4o5o6
    snprintf(path, sizeof(path), NG_RES_ROOT "boot.js");
    return mod_scene_load_js_path(ctx, path, false);
  }
  const bool helpers = mod_scene_wants_helpers(scene_id);
  snprintf(path, sizeof(path), NG_RES_ROOT "scenes/%s.js", scene_id);
  if (mod_scene_load_js_path(ctx, path, helpers)) {
    return true;
  }
  if (!mod_scene_load_helpers(ctx->ctx)) {
    return false;
  }
  snprintf(path, sizeof(path), "tests/scenes/%s.js", scene_id);
  if (mod_scene_load_js_path(ctx, path, false)) {
    return true;
  }
  NG_LOG_ERROR("scene js missing: %s", scene_id);
  return false;
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

static void mod_scene_drain_pending_change(ModSceneCtx *ctx) {
  if (!ctx || !ctx->pending_scene_id[0] || !mod_scene_is_server()) {
    return;
  }
  char next_scene[32];
  strncpy(next_scene, ctx->pending_scene_id, sizeof(next_scene) - 1);
  next_scene[sizeof(next_scene) - 1] = '\0';
  ctx->pending_scene_id[0] = '\0';
  if (strcmp(next_scene, ctx->scene_id) == 0) {
    return;
  }
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
  char reply[256];
  if (mod_sim_load_scene) {
    mod_sim_load_scene(next_scene, reply, sizeof(reply));
  }
#endif
}

static void mod_scene_call_lifecycle(ModSceneCtx *ctx, const char *method) {
  mod_scene_call_method(ctx, method, 0);
  mod_scene_drain_pending_change(ctx);
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

static void mod_scene_pull_entity_phase(ModSceneCtx *ctx, NgSceneInst *inst) {
  if (!ctx->ctx || !inst) {
    return;
  }
  char key[48];
  snprintf(key, sizeof(key), "inst_%d", inst->handle);
  duk_push_global_stash(ctx->ctx);
  duk_get_prop_string(ctx->ctx, -1, key);
  if (!duk_is_object(ctx->ctx, -1)) {
    duk_pop_n(ctx->ctx, 2);
    return;
  }
  duk_get_prop_string(ctx->ctx, -1, "phase");
  if (duk_is_number(ctx->ctx, -1)) {
    inst->phase = (float)duk_get_number(ctx->ctx, -1);
  }
  duk_pop_n(ctx->ctx, 3);
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
    mod_scene_drain_pending_change(ctx);
    if (on_server && inst->sync == NG_SYNC_SERVER) {
      mod_scene_pull_entity_phase(ctx, (NgSceneInst *)inst);
    }
  }
}

static void mod_scene_set_active_for(ModSceneCtx *ctx) {
  if (ctx == &g_scene_server.scene) {
    mod_scene_runtime_use_server();
  } else if (ctx == &g_scene_view.scene) {
    mod_scene_runtime_use_view();
  }
}

static void mod_scene_unload(ModSceneCtx *ctx) {
  if (!ctx) {
    return;
  }
  mod_scene_set_active_for(ctx);
  if (ctx->native) {
    mod_scene_native_unload();
  } else if (ctx->ctx && ctx->started) {
    mod_scene_call_lifecycle(ctx, "stop");
    mod_scene_call_lifecycle(ctx, "dispose");
  }
  if (ctx->ctx) {
    duk_destroy_heap(ctx->ctx);
    ctx->ctx = NULL;
  }
  mod_scene_graph_reset();
  mod_scene_assets_reset();
  ctx->loaded = false;
  ctx->inited = false;
  ctx->started = false;
  ctx->native = false;
  ctx->is_controller = false;
  ctx->is_server_host = false;
  ctx->scene_id[0] = '\0';
}

static bool mod_scene_begin(const char *scene_id, bool server_host, bool is_controller) {
  ModSceneCtx *ctx = mod_scene_runtime_scene();
  mod_scene_unload(ctx);
  strncpy(ctx->scene_id, scene_id, sizeof(ctx->scene_id) - 1);
  ctx->is_server_host = server_host;
  ctx->is_controller = is_controller;
  mod_scene_graph_reset();
  mod_scene_assets_reset();
  ctx->ctx = duk_create_heap_default();
  if (!ctx->ctx) {
    return false;
  }
  if (mod_scene_load_js(ctx, scene_id)) {
    ctx->loaded = true;
    mod_scene_call_lifecycle(ctx, "init");
    ctx->inited = true;
    return true;
  }
  duk_destroy_heap(ctx->ctx);
  ctx->ctx = NULL;
  // agent: composer-2.5 | 2026-07-28 | wire native scene fallback path | 096c5c
  if (mod_scene_native_load(scene_id)) {
    ctx->native = true;
    ctx->loaded = true;
    ctx->inited = true;
    return true;
  }
  mod_scene_unload(ctx);
  return false;
}

bool mod_scene_load(const char *scene_id) {
  mod_scene_runtime_use_server();
  if (!scene_id || scene_id[0] == '\0') {
    mod_scene_unload(mod_scene_runtime_scene());
    return true;
  }
  ModSceneCtx *ctx = mod_scene_runtime_scene();
  if (!mod_scene_begin(scene_id, true, true)) {
    return false;
  }
  if (ctx->native) {
    ctx->started = true;
    return true;
  }
  NgSessionState session = {0};
  strncpy(session.scene_id, scene_id, sizeof(session.scene_id) - 1);
  mod_scene_push_session_obj(ctx, &session);
  if (!mod_scene_call_method(ctx, "start", 1)) {
    mod_scene_unload(ctx);
    return false;
  }
  mod_scene_drain_pending_change(ctx);
  ctx->started = true;
  return true;
}

bool mod_scene_load_boot(void) { return mod_scene_load("boot"); }

bool mod_scene_is_loaded(void) {
  mod_scene_runtime_use_server();
  return NG_SCENE_ACTIVE()->loaded;
}

const char *mod_scene_current_id(void) {
  mod_scene_runtime_use_server();
  return NG_SCENE_ACTIVE()->scene_id;
}

int mod_scene_entity_count(void) {
  mod_scene_runtime_use_server();
  return mod_scene_graph_inst_count();
}

void mod_scene_fill_session(NgSessionState *session) {
  if (!session) {
    return;
  }
  mod_scene_runtime_use_server();
  mod_scene_graph_fill_session_spawns(session);
}

bool mod_scene_is_controller(void) {
  mod_scene_runtime_use_server();
  return NG_SCENE_ACTIVE()->is_controller;
}

bool mod_scene_can_author(NgSyncMode sync) {
  mod_scene_runtime_use_server();
  if (sync == NG_SYNC_SHARED) {
    return true;
  }
  if (sync == NG_SYNC_OWNER) {
    return NG_SCENE_ACTIVE()->is_controller;
  }
  return false;
}

static void mod_scene_on_session_ctx(ModSceneCtx *ctx, const NgSessionState *session,
                                     bool force_reload) {
  // agent: composer-2.5 | 2026-07-29 | force reload on view session | 8b87bd
  mod_scene_set_active_for(ctx);
  if (!session) {
    return;
  }

  if (!ctx->loaded || strcmp(ctx->scene_id, session->scene_id) != 0 || force_reload) {
    if (!mod_scene_begin(session->scene_id, false,
                         session->your_id != 0 && session->your_id == session->controller_id)) {
      return;
    }
    ctx->started = false;
  } else {
    ctx->is_controller =
        (session->your_id != 0 && session->your_id == session->controller_id);
  }

  if (ctx->native) {
    if (!ctx->started) {
      ctx->started = true;
    }
#ifndef NG_SERVER
    mod_render_apply_session(session);
#endif
    return;
  }

  for (int i = 0; i < session->spawn_count; i++) {
    mod_scene_route_registry_spawn(ctx, &session->spawns[i]);
  }

  if (!ctx->started) {
    mod_scene_push_session_obj(ctx, session);
    mod_scene_call_method(ctx, "start", 1);
    mod_scene_drain_pending_change(ctx);
    ctx->started = true;
  }

#ifndef NG_SERVER
  mod_render_apply_session(session);
#endif
}

void mod_scene_on_session(const NgSessionState *session) {
  mod_scene_runtime_use_server();
  mod_scene_on_session_ctx(mod_scene_runtime_scene(), session, false);
}

void mod_scene_view_on_session(const NgSessionState *session) {
  mod_scene_runtime_use_view();
  mod_scene_on_session_ctx(mod_scene_runtime_scene(), session, true);
}

void mod_scene_apply_remote(const NgStateUpdate *update) {
  mod_scene_runtime_use_server();
  if (!update || !NG_SCENE_ACTIVE()->loaded) {
    return;
  }
  mod_scene_graph_apply_update(update);
}

void mod_scene_view_apply_remote(const NgStateUpdate *update) {
  mod_scene_runtime_use_view();
  if (!update || !NG_SCENE_ACTIVE()->loaded) {
    return;
  }
  mod_scene_graph_apply_update(update);
}

bool mod_scene_take_flush(NgStateUpdate *out) {
  mod_scene_runtime_use_server();
  if (!out || !NG_SCENE_ACTIVE()->loaded) {
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

static bool mod_scene_tick_ctx(ModSceneCtx *ctx, float dt) {
  if (!ctx || !ctx->loaded || !ctx->started) {
    return true;
  }
  mod_scene_set_active_for(ctx);

  if (ctx->native) {
    mod_scene_native_step(dt);
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
    if (mod_scene_is_server()) {
      mod_scene_mirror_server(mod_sim_world());
    }
#endif
    return true;
  }

  if (!ctx->ctx) {
    return true;
  }

  mod_input_begin_frame();
  duk_push_number(ctx->ctx, dt);
  mod_scene_call_method(ctx, "step", 1);
  mod_scene_run_entity_steps(ctx, dt);
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
  if (mod_scene_is_server()) {
    mod_scene_mirror_server(mod_sim_world());
  }
#endif
  return true;
}

static bool mod_scene_on_msg(const NgMsg *msg, void *vctx) {
  (void)vctx;
  if (!msg || msg->kind != NG_MSG_TICK || msg->to != NG_BUS_ANY) {
    return false;
  }
  mod_scene_tick_ctx(&g_scene_server.scene, msg->dt);
  mod_scene_tick_ctx(&g_scene_view.scene, msg->dt);
  mod_scene_runtime_use_server();
  mod_net_flush_scene_updates();
  return true;
}

static bool mod_scene_init(void *vctx) {
  (void)vctx;
  mod_scene_runtime_use_server();
  memset(&g_scene_server, 0, sizeof(g_scene_server));
  mod_scene_graph_reset();
  mod_scene_runtime_use_view();
  memset(&g_scene_view, 0, sizeof(g_scene_view));
  mod_scene_graph_reset();
  mod_scene_runtime_use_server();
  return true;
}

static void mod_scene_shutdown(void *vctx) {
  (void)vctx;
  mod_scene_unload(&g_scene_view.scene);
  mod_scene_unload(&g_scene_server.scene);
  mod_scene_runtime_use_server();
}

static const NgModOps g_scene_ops = {
    .name = "scene",
    .dest = NG_BUS_SCENE,
    .init = mod_scene_init,
    .shutdown = mod_scene_shutdown,
    .on_msg = mod_scene_on_msg,
};

const NgModOps *mod_scene_ops(void) { return &g_scene_ops; }

void *mod_scene_ctx(void) { return &g_scene_server.scene; }

bool mod_scene_graph_active(void) {
  mod_scene_runtime_use_server();
  return mod_scene_graph_inst_count() > 0;
}

bool mod_scene_view_is_loaded(void) {
  mod_scene_runtime_use_view();
  return NG_SCENE_ACTIVE()->loaded;
}

const char *mod_scene_view_current_id(void) {
  mod_scene_runtime_use_view();
  return NG_SCENE_ACTIVE()->scene_id;
}

int mod_scene_view_entity_count(void) {
  mod_scene_runtime_use_view();
  return mod_scene_graph_inst_count();
}

void mod_scene_view_status_text(char *out, size_t cap) {
  // agent: composer-2.5 | 2026-07-29 | view status text helper | d2e790
  if (!out || cap == 0) {
    return;
  }
  mod_scene_runtime_use_view();
  const char *id = NG_SCENE_ACTIVE()->scene_id;
  snprintf(out, cap, "view scene=%s loaded=%d graph=%d entities=%d", id && id[0] ? id : "",
           NG_SCENE_ACTIVE()->loaded ? 1 : 0, mod_scene_graph_inst_count(),
           mod_scene_graph_inst_count());
}

bool mod_scene_view_graph_active(void) {
  mod_scene_runtime_use_view();
  return mod_scene_graph_inst_count() > 0;
}

void mod_scene_mirror_server(NgWorld *w) {
  mod_scene_runtime_use_server();
  if (!w || !NG_SCENE_ACTIVE()->loaded) {
    return;
  }
  const int n = mod_scene_graph_inst_count();
  for (int i = 0; i < n; i++) {
    const NgSceneInst *cinst = mod_scene_graph_inst_at(i);
    if (!cinst) {
      continue;
    }
    NgSceneInst *inst = mod_scene_graph_inst_by_handle(cinst->handle);
    if (!inst || inst->sync != NG_SYNC_SERVER) {
      continue;
    }
    NgSceneResolvedModel resolved;
    NgEntityType type = NG_ENTITY_SPHERE;
    if (mod_scene_assets_resolve_model(inst->model, &resolved) && resolved.ok) {
      type = mod_scene_assets_entity_type_for_kind(resolved.mesh_kind);
    }
    if (inst->world_id == 0) {
      inst->world_id =
          ng_world_spawn(w, type, inst->pos[0], inst->pos[1], inst->pos[2]);
      if (inst->world_id != 0) {
        ng_world_set_public_id(w, inst->world_id, inst->id);
      }
    }
    if (inst->world_id != 0) {
      ng_world_set_entity_state(w, inst->world_id, inst->pos[0], inst->pos[1], inst->pos[2],
                                inst->rot[1], inst->phase);
    }
  }
}

void mod_scene_apply_snapshot(const NgSnapshot *snap) {
  mod_scene_runtime_use_server();
  if (!snap || !NG_SCENE_ACTIVE()->loaded) {
    return;
  }
  for (int i = 0; i < snap->entity_count; i++) {
    const NgEntitySnap *e = &snap->entities[i];
    NgSceneInst *inst = mod_scene_graph_inst_by_id(e->id);
    if (!inst || inst->sync != NG_SYNC_SERVER) {
      continue;
    }
    inst->pos[0] = e->pos[0];
    inst->pos[1] = e->pos[1];
    inst->pos[2] = e->pos[2];
    inst->rot[1] = e->rot_y;
    inst->phase = e->phase;
  }
}

void mod_scene_view_apply_snapshot(const NgSnapshot *snap) {
  mod_scene_runtime_use_view();
  if (!snap || !NG_SCENE_ACTIVE()->loaded) {
    return;
  }
  for (int i = 0; i < snap->entity_count; i++) {
    const NgEntitySnap *e = &snap->entities[i];
    NgSceneInst *inst = mod_scene_graph_inst_by_id(e->id);
    if (!inst || inst->sync != NG_SYNC_SERVER) {
      continue;
    }
    inst->pos[0] = e->pos[0];
    inst->pos[1] = e->pos[1];
    inst->pos[2] = e->pos[2];
    inst->rot[1] = e->rot_y;
    inst->phase = e->phase;
  }
}

// agent: composer-2.5 | 2026-07-27 | smoke cube registry sphere inst | c5f796
static bool mod_scene_smoke_one(const char *scene_id, const char *spawn_desc, bool expect_inst) {
  mod_scene_runtime_use_server();
  if (!mod_scene_begin(scene_id, true, true)) {
    return false;
  }
  ModSceneCtx *ctx = mod_scene_runtime_scene();
  NgSessionState session = {0};
  strncpy(session.scene_id, scene_id, sizeof(session.scene_id) - 1);
  mod_scene_push_session_obj(ctx, &session);
  if (!mod_scene_call_method(ctx, "start", 1)) {
    mod_scene_unload(ctx);
    return false;
  }
  mod_scene_drain_pending_change(ctx);
  ctx->started = true;
  const bool ok =
      expect_inst ? (mod_scene_graph_inst_count() >= 1)
                  : (mod_scene_graph_registry_id_for_desc(spawn_desc) != 0);
  mod_scene_unload(ctx);
  return ok;
}

bool mod_scene_is_native(void) {
  mod_scene_runtime_use_server();
  return NG_SCENE_ACTIVE()->native;
}

bool mod_scene_smoke_test(void) {
  return mod_scene_smoke_one("cube", "cube_a_e", false) &&
         mod_scene_smoke_one("sphere", "sphere_a_e", true) &&
         mod_scene_smoke_one("owner", "owner_e", false) &&
         mod_scene_smoke_one("local", "local_e", false);
}

// agent: composer-2.5 | 2026-07-29 | view status text helper | d2e790
// agent: composer-2.5 | 2026-07-29 | host server view split | 1b39ad
// agent: composer-2.5 | 2026-07-28 | wire native scene fallback path | 096c5c
// agent: composer-2.5 | 2026-07-28 | test helpers only for fixtures | t4h5e6
// agent: composer-2.5 | 2026-07-28 | boot js outside scenes dir | b4o5o6
// agent: composer-2.5 | 2026-07-29 | view reset after scene begin | ef3462
// agent: composer-2.5 | 2026-07-29 | js change scene binding and drain | d70ea8
