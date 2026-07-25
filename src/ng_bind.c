// agent: composer-2.5 | 2026-07-25 | duktape native bindings | 8d2b4e
#include "ng_bind.h"
#include "ng_cli.h"
#include <raylib.h>

static duk_ret_t bind_cli_dispatch(duk_context *ctx) {
  const int argc = duk_get_top(ctx);
  const char *argv[16];
  const int n = (argc > 16) ? 16 : argc;

  for (int i = 0; i < n; i++) {
    argv[i] = duk_require_string(ctx, i);
  }

  ng_cli_feedback(n, argv);
  return 0;
}

void ng_bind_register(duk_context *ctx) {
  duk_push_c_function(ctx, bind_cli_dispatch, DUK_VARARGS);
  duk_put_global_string(ctx, "ng_cli_dispatch");
}
