// agent: composer-2.5 | 2026-07-26 | client scene JS host | b8c9d0
#include "mod_scene.h"
#include "core/ng_fs.h"
#include "core/ng_log.h"
#include "core/ng_session.h"
#include "mod/mod_input.h"
#include "mod/mod_render.h"
#include "ng_path.h"
#include "vendor/duktape.h"
#include <string.h>

typedef struct ModSceneCtx {
  duk_context *ctx;
  char scene_id[32];
  bool client_fields;
  bool is_controller;
  bool loaded;
  bool dirty;
  uint32_t entity_id;
  float rot_y;
} ModSceneCtx;

static ModSceneCtx g_scene_ctx;

static bool mod_scene_call_bool(duk_context *ctx, const char *fn) {
  duk_get_global_string(ctx, fn);
  if (!duk_is_function(ctx, -1)) {
    duk_pop(ctx);
    return false;
  }
  if (duk_pcall(ctx, 0) != DUK_EXEC_SUCCESS) {
    NG_LOG_ERROR("scene %s: %s", fn, duk_safe_to_string(ctx, -1));
    duk_pop(ctx);
    return false;
  }
  const bool r = duk_to_boolean(ctx, -1);
  duk_pop(ctx);
  return r;
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
  return ok;
}

static void mod_scene_unload(ModSceneCtx *ctx) {
  if (ctx->ctx) {
    duk_destroy_heap(ctx->ctx);
    ctx->ctx = NULL;
  }
  ctx->loaded = false;
  ctx->client_fields = false;
  ctx->is_controller = false;
  ctx->entity_id = 0;
  ctx->dirty = false;
  ctx->scene_id[0] = '\0';
}

bool mod_scene_client_fields_active(void) { return g_scene_ctx.client_fields && g_scene_ctx.loaded; }

bool mod_scene_is_controller(void) {
  return g_scene_ctx.client_fields && g_scene_ctx.loaded && g_scene_ctx.is_controller;
}

float mod_scene_get_rot_y(void) {
  if (g_scene_ctx.loaded && g_scene_ctx.client_fields && g_scene_ctx.ctx) {
    duk_get_global_string(g_scene_ctx.ctx, "scene_get_rot_y");
    if (duk_is_function(g_scene_ctx.ctx, -1)) {
      if (duk_pcall(g_scene_ctx.ctx, 0) == DUK_EXEC_SUCCESS) {
        const double v = duk_to_number(g_scene_ctx.ctx, -1);
        duk_pop(g_scene_ctx.ctx);
        return (float)v;
      }
      duk_pop(g_scene_ctx.ctx);
    } else {
      duk_pop(g_scene_ctx.ctx);
    }
  }
  return g_scene_ctx.rot_y;
}

void mod_scene_on_session(const NgSessionState *session) {
  ModSceneCtx *ctx = &g_scene_ctx;
  if (!session) {
    return;
  }

  if (!session->client_fields) {
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
    if (!mod_scene_load_js(ctx, session->scene_id)) {
      mod_scene_unload(ctx);
      return;
    }
    ctx->loaded = true;
  }

  ctx->client_fields = true;
  ctx->is_controller = (session->your_id != 0 && session->your_id == session->controller_id);
  ctx->entity_id = session->cube_entity_id;

  duk_get_global_string(ctx->ctx, "scene_on_session");
  if (duk_is_function(ctx->ctx, -1)) {
    duk_push_int(ctx->ctx, (duk_int_t)session->cube_entity_id);
    duk_push_boolean(ctx->ctx, ctx->is_controller ? 1 : 0);
    if (duk_pcall(ctx->ctx, 2) != DUK_EXEC_SUCCESS) {
      NG_LOG_ERROR("scene_on_session: %s", duk_safe_to_string(ctx->ctx, -1));
    }
    duk_pop(ctx->ctx);
  } else {
    duk_pop(ctx->ctx);
  }

  mod_render_apply_session(session);
  ctx->dirty = ctx->is_controller;
}

void mod_scene_apply_remote(const NgStateUpdate *update) {
  ModSceneCtx *ctx = &g_scene_ctx;
  if (!update || !ctx->loaded || !ctx->client_fields || ctx->is_controller) {
    return;
  }
  duk_get_global_string(ctx->ctx, "scene_apply_remote");
  if (duk_is_function(ctx->ctx, -1)) {
    duk_push_int(ctx->ctx, (duk_int_t)update->entity_id);
    duk_push_number(ctx->ctx, update->rot_y);
    if (duk_pcall(ctx->ctx, 2) != DUK_EXEC_SUCCESS) {
      NG_LOG_ERROR("scene_apply_remote: %s", duk_safe_to_string(ctx->ctx, -1));
    }
    duk_pop(ctx->ctx);
  } else {
    duk_pop(ctx->ctx);
  }
  ctx->rot_y = mod_scene_get_rot_y();
  mod_render_set_cube_rot_y(ctx->rot_y);
}

bool mod_scene_take_flush(NgStateUpdate *out) {
  ModSceneCtx *ctx = &g_scene_ctx;
  if (!out || !ctx->loaded || !ctx->client_fields || !ctx->is_controller || !ctx->dirty) {
    return false;
  }
  duk_get_global_string(ctx->ctx, "scene_flush");
  if (!duk_is_function(ctx->ctx, -1)) {
    duk_pop(ctx->ctx);
    return false;
  }
  if (duk_pcall(ctx->ctx, 0) != DUK_EXEC_SUCCESS) {
    NG_LOG_ERROR("scene_flush: %s", duk_safe_to_string(ctx->ctx, -1));
    duk_pop(ctx->ctx);
    return false;
  }
  if (!duk_is_object(ctx->ctx, -1)) {
    duk_pop(ctx->ctx);
    return false;
  }
  duk_get_prop_string(ctx->ctx, -1, "entity_id");
  duk_get_prop_string(ctx->ctx, -2, "rot_y");
  out->entity_id = (uint32_t)duk_to_int(ctx->ctx, -2);
  out->rot_y = (float)duk_to_number(ctx->ctx, -1);
  out->tick = 0;
  duk_pop_n(ctx->ctx, 3);
  ctx->rot_y = out->rot_y;
  mod_render_set_cube_rot_y(out->rot_y);
  ctx->dirty = false;
  return true;
}

static bool mod_scene_on_msg(const NgMsg *msg, void *vctx) {
  ModSceneCtx *ctx = (ModSceneCtx *)vctx;
  if (!ctx || !msg) {
    return false;
  }
  if (msg->kind != NG_MSG_TICK || msg->to != NG_BUS_ANY) {
    return false;
  }
  if (!ctx->loaded || !ctx->client_fields || !ctx->is_controller || !ctx->ctx) {
    return true;
  }
  duk_get_global_string(ctx->ctx, "scene_tick");
  if (!duk_is_function(ctx->ctx, -1)) {
    duk_pop(ctx->ctx);
    return true;
  }
  duk_push_number(ctx->ctx, msg->dt);
  duk_push_int(ctx->ctx, mod_input_buttons());
  duk_push_number(ctx->ctx, mod_input_take_yaw());
  if (duk_pcall(ctx->ctx, 3) != DUK_EXEC_SUCCESS) {
    NG_LOG_ERROR("scene_tick: %s", duk_safe_to_string(ctx->ctx, -1));
  }
  duk_pop(ctx->ctx);
  ctx->dirty = true;
  return true;
}

static bool mod_scene_init(void *vctx) {
  (void)vctx;
  memset(&g_scene_ctx, 0, sizeof(g_scene_ctx));
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
