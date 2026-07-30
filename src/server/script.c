// agent: composer-2.5 | 2026-07-25 | script bus module | 3e9a5d
#include "script.h"
#include "engine/ng_bus.h"
#include "engine/ng_fs.h"
#include "engine/ng_log.h"
#include "ng_path.h"
#include "vendor/duktape.h"
#include <string.h>

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

static void mod_script_bind(duk_context *ctx) {
  duk_push_c_function(ctx, bind_bus_send, DUK_VARARGS);
  duk_put_global_string(ctx, "ng_bus_send");
  duk_push_c_function(ctx, bind_bus_reply, 1);
  duk_put_global_string(ctx, "ng_bus_reply");
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
