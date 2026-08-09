// agent: composer-2.5 | 2026-07-25 | script bus module | 3e9a5d
// agent: composer-2.5 | 2026-08-09 | script CLI load set status bind | 495d21
#include "script.h"
#include "engine/ng_bus.h"
#include "engine/ng_fs.h"
#include "engine/ng_log.h"
#include "ng_path.h"
#include "vendor/duktape.h"
#include <stdio.h>
#include <string.h>

#if !defined(NG_SERVER)
#include "client/ng_app_client.h"
#include "client/render.h"
#include "net/mod_net.h"
#include "scene/scene.h"
#include "server/agent.h"
#endif

typedef struct ModScriptCtx {
  duk_context *ctx;
} ModScriptCtx;

static ModScriptCtx g_script_ctx;

static duk_ret_t bind_bus_send(duk_context *ctx) {
  const char *dest_name = duk_require_string(ctx, 0);
  const NgBusDest dest = ng_bus_dest_from_string(dest_name);
  const int argc = duk_get_top(ctx) - 1;
  const char *argv[NG_BUS_ARGV_MAX];
  const int n = (argc > NG_BUS_ARGV_MAX) ? NG_BUS_ARGV_MAX : argc;

  for (int i = 0; i < n; i++) {
    argv[i] = duk_require_string(ctx, i + 1);
  }

  NgMsg msg = {
      .kind = NG_MSG_CMD,
      .from = NG_BUS_SCRIPT,
      .to = dest,
      .argc = n,
      .argv = argv,
  };
  ng_bus_publish(&msg);
  return 0;
}

static duk_ret_t bind_bus_cmd(duk_context *ctx) {
  const char *dest_name = duk_require_string(ctx, 0);
  const char *line = duk_require_string(ctx, 1);
  const NgBusDest dest = ng_bus_dest_from_string(dest_name);
  NgMsg msg = {
      .kind = NG_MSG_CMD,
      .from = NG_BUS_SCRIPT,
      .to = dest,
      .line = line,
  };
  ng_bus_publish(&msg);
  return 0;
}

static duk_ret_t bind_bus_reply(duk_context *ctx) {
  const char *text = duk_require_string(ctx, 0);
  NgMsg msg = {
      .kind = NG_MSG_REPLY,
      .from = NG_BUS_SCRIPT,
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
      .to = NG_BUS_ANY,
#else
      .to = NG_BUS_CONSOLE,
#endif
      .text = text,
  };
  ng_bus_publish(&msg);
  return 0;
}

static bool mod_script_run_file(ModScriptCtx *ctx, const char *path);

static duk_ret_t bind_script_load(duk_context *ctx) {
  const char *rel = duk_require_string(ctx, 0);
  char path[256];
  if (rel[0] == '/') {
    snprintf(path, sizeof(path), "%s", rel);
  } else {
    snprintf(path, sizeof(path), NG_RES_ROOT "%s", rel);
  }
  const bool ok = mod_script_run_file(&g_script_ctx, path);
  duk_push_boolean(ctx, ok ? 1 : 0);
  return 1;
}

static duk_ret_t bind_render_set(duk_context *ctx) {
  const char *path = duk_require_string(ctx, 0);
  const char *value = duk_require_string(ctx, 1);
#if defined(NG_SERVER)
  (void)path;
  (void)value;
  duk_push_false(ctx);
#else
  duk_push_boolean(ctx, mod_render_set(path, value) ? 1 : 0);
#endif
  return 1;
}

static duk_ret_t bind_render_get(duk_context *ctx) {
  const char *path = duk_require_string(ctx, 0);
  char buf[64];
#if defined(NG_SERVER)
  (void)path;
  buf[0] = '\0';
#else
  if (!mod_render_get(path, buf, sizeof(buf))) {
    buf[0] = '\0';
  }
#endif
  duk_push_string(ctx, buf);
  return 1;
}

static duk_ret_t bind_cli_status(duk_context *ctx) {
  char status[800];
#if defined(NG_SERVER)
  snprintf(status, sizeof(status), "status n/a");
#else
  char host_line[64] = {0};
  uint16_t ep_port = 0;
  char root_line[128] = {0};
  char view_line[128] = {0};
  char render_line[256] = {0};
  char vis_line[128] = {0};
  char up_host[64] = {0};
  uint16_t up_port = 0;
  char auth_line[32] = {0};
  mod_net_endpoint(host_line, sizeof(host_line), &ep_port);
  mod_net_root_mirror_text(root_line, sizeof(root_line));
  mod_scene_view_status_text(view_line, sizeof(view_line));
  mod_render_snapshot_text(render_line, sizeof(render_line));
  mod_render_visibility_text(vis_line, sizeof(vis_line));
  mod_net_upstream_endpoint(up_host, sizeof(up_host), &up_port);
  snprintf(auth_line, sizeof(auth_line), "%s", mod_net_is_authoritative() ? "local" : "upstream");
  const uint16_t listen_port = mod_agent_listening_port();
  const double elapsed = mod_net_connect_elapsed();
  const char *launch_mode = ng_app_client_mode_text();
  snprintf(status, sizeof(status),
           "status\n"
           "launch=%s\n"
           "gw=%s\n"
           "client=%s:%u\n"
           "upstream=%s:%u conn=%d\n"
           "agent_listen=%u elapsed=%.1fs\n"
           "%s\n"
           "%s\n"
           "%s\n"
           "%s",
           launch_mode, auth_line, host_line, ep_port, up_host[0] ? up_host : "-", up_port,
           mod_net_upstream_connected() ? 1 : 0, listen_port, elapsed, root_line, view_line, vis_line,
           render_line);
#endif
  duk_push_string(ctx, status);
  return 1;
}

static void mod_script_bind(duk_context *ctx) {
  duk_push_c_function(ctx, bind_bus_send, DUK_VARARGS);
  duk_put_global_string(ctx, "ng_bus_send");
  duk_push_c_function(ctx, bind_bus_cmd, 2);
  duk_put_global_string(ctx, "ng_bus_cmd");
  duk_push_c_function(ctx, bind_bus_reply, 1);
  duk_put_global_string(ctx, "ng_bus_reply");
  duk_push_c_function(ctx, bind_script_load, 1);
  duk_put_global_string(ctx, "ng_script_load");
  duk_push_c_function(ctx, bind_render_set, 2);
  duk_put_global_string(ctx, "ng_render_set");
  duk_push_c_function(ctx, bind_render_get, 1);
  duk_put_global_string(ctx, "ng_render_get");
  duk_push_c_function(ctx, bind_cli_status, 0);
  duk_put_global_string(ctx, "ng_cli_status");
}

static bool mod_script_run_file(ModScriptCtx *ctx, const char *path) {
  char *text = ng_fs_read_text(path);
  if (!text) {
    NG_LOG_ERROR("script file not found: %s", path);
    return false;
  }
  duk_push_string(ctx->ctx, text);
  const bool ok = (duk_peval(ctx->ctx) == DUK_EXEC_SUCCESS);
  if (!ok) {
    NG_LOG_ERROR("script error: %s", duk_safe_to_string(ctx->ctx, -1));
  }
  duk_pop(ctx->ctx);
  ng_fs_free_text(text);
  return ok;
}

static bool mod_script_exec_line(ModScriptCtx *ctx, const char *line) {
  if (!ctx->ctx || !line) {
    return false;
  }
  duk_get_global_string(ctx->ctx, "ng_bus_exec_line");
  if (!duk_is_function(ctx->ctx, -1)) {
    NG_LOG_ERROR("ng_bus_exec_line missing in bus.js");
    duk_pop(ctx->ctx);
    return false;
  }
  duk_push_string(ctx->ctx, line);
  if (duk_pcall(ctx->ctx, 1) != DUK_EXEC_SUCCESS) {
    NG_LOG_ERROR("bus.js error: %s", duk_safe_to_string(ctx->ctx, -1));
    duk_pop(ctx->ctx);
    return false;
  }
  duk_pop(ctx->ctx);
  return true;
}

static bool mod_script_on_msg(const NgMsg *msg, void *vctx) {
  ModScriptCtx *ctx = (ModScriptCtx *)vctx;
  if (!ctx || !msg) {
    return false;
  }

  if (msg->kind == NG_MSG_CMD && msg->line) {
    return mod_script_exec_line(ctx, msg->line);
  }
  return false;
}

static bool mod_script_init(void *vctx) {
  ModScriptCtx *ctx = (ModScriptCtx *)vctx;
  ctx->ctx = duk_create_heap_default();
  if (!ctx->ctx) {
    NG_LOG_ERROR("duktape heap failed");
    return false;
  }
  mod_script_bind(ctx->ctx);
  return mod_script_run_file(ctx, NG_RES_ROOT "bus.js");
}

static void mod_script_shutdown(void *vctx) {
  ModScriptCtx *ctx = (ModScriptCtx *)vctx;
  if (ctx->ctx) {
    duk_destroy_heap(ctx->ctx);
    ctx->ctx = NULL;
  }
}

// agent: composer-2.5 | 2026-07-29 | Extend NgModOps side fixed_step | 8020d0
static const NgModOps g_script_ops = {
    .name = "script",
    .dest = NG_BUS_SCRIPT,
    .side = NG_MOD_SIDE_BOTH,
    .init = mod_script_init,
    .shutdown = mod_script_shutdown,
    .on_msg = mod_script_on_msg,
    .fixed_step = NULL,
};

const NgModOps *mod_script_ops(void) { return &g_script_ops; }

void *mod_script_ctx(void) { return &g_script_ctx; }
// agent: composer-2.5 | 2026-07-29 | Extend NgModOps side fixed_step | 8020d0
// agent: composer-2.5 | 2026-08-09 | script CLI load set status bind | 495d21
