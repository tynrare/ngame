// agent: composer-2.5 | 2026-07-25 | console bus module | 6b2e9a
#include "mod_console.h"
#include "core/ng_bus.h"
#include <raylib.h>
#include <string.h>

#define MOD_CONSOLE_INPUT_MAX 256
#define MOD_CONSOLE_OUTPUT_MAX 1024
#define MOD_CONSOLE_LINES 15
#define MOD_CONSOLE_LINE_H 20

typedef struct ModConsoleCtx {
  char input[MOD_CONSOLE_INPUT_MAX];
  char last_cmd[MOD_CONSOLE_INPUT_MAX];
  char output[MOD_CONSOLE_OUTPUT_MAX];
  int input_len;
  bool open;
  bool submitted;
} ModConsoleCtx;

static ModConsoleCtx g_console_ctx;

static void mod_console_set_output(ModConsoleCtx *ctx, const char *text) {
  if (!ctx || !text) {
    return;
  }
  strncpy(ctx->output, text, MOD_CONSOLE_OUTPUT_MAX - 1);
  ctx->output[MOD_CONSOLE_OUTPUT_MAX - 1] = '\0';
}

static void mod_console_update_input(ModConsoleCtx *ctx) {
  if (IsKeyPressed(KEY_TAB)) {
    ctx->open = !ctx->open;
    ctx->submitted = false;
    ng_bus_set_gate(NG_BUS_RENDER, !ctx->open);
    return;
  }

  if (!ctx->open) {
    return;
  }

  int key = GetCharPressed();
  while (key > 0) {
    if (key >= 32 && key <= 125 && ctx->input_len < MOD_CONSOLE_INPUT_MAX - 1) {
      ctx->input[ctx->input_len++] = (char)key;
      ctx->input[ctx->input_len] = '\0';
    }
    key = GetCharPressed();
  }

  if (IsKeyPressed(KEY_BACKSPACE) && ctx->input_len > 0) {
    ctx->input_len--;
    ctx->input[ctx->input_len] = '\0';
  }

  if (IsKeyPressed(KEY_ENTER)) {
    ctx->submitted = true;
  }
}

static void mod_console_submit(ModConsoleCtx *ctx) {
  if (!ctx->submitted || ctx->input_len == 0) {
    ctx->submitted = false;
    return;
  }

  strncpy(ctx->last_cmd, ctx->input, MOD_CONSOLE_INPUT_MAX - 1);
  ctx->last_cmd[MOD_CONSOLE_INPUT_MAX - 1] = '\0';

  NgMsg msg = {
      .kind = NG_MSG_CMD,
      .from = NG_BUS_CONSOLE,
      .to = NG_BUS_NET,
      .line = ctx->last_cmd,
  };
  ng_bus_publish(&msg);

  ctx->input[0] = '\0';
  ctx->input_len = 0;
  ctx->submitted = false;
}

static void mod_console_draw(const ModConsoleCtx *ctx) {
  if (!ctx->open) {
    return;
  }

  const int w = GetScreenWidth() - 2;
  const int panel_h = MOD_CONSOLE_LINE_H * MOD_CONSOLE_LINES;

  DrawRectangle(1, 1, w, panel_h, (Color){0, 0, 0, 100});
  DrawText(TextFormat("> %s", ctx->last_cmd), 5, 2, MOD_CONSOLE_LINE_H, WHITE);
  DrawText(ctx->output, 10, MOD_CONSOLE_LINE_H + 2, MOD_CONSOLE_LINE_H, WHITE);

  const int row_y = 1 + MOD_CONSOLE_LINE_H * (MOD_CONSOLE_LINES - 1);
  DrawRectangle(1, row_y, w, MOD_CONSOLE_LINE_H, Fade(BLACK, 0.5f));
  DrawRectangleLines(1, row_y, w, MOD_CONSOLE_LINE_H, GREEN);
  DrawText(TextFormat("< %s", ctx->input), 5, row_y + 2, MOD_CONSOLE_LINE_H, WHITE);
}

static bool mod_console_on_msg(const NgMsg *msg, void *vctx) {
  ModConsoleCtx *ctx = (ModConsoleCtx *)vctx;
  if (!ctx || !msg) {
    return false;
  }

  switch (msg->kind) {
  case NG_MSG_TICK:
    mod_console_update_input(ctx);
    mod_console_submit(ctx);
    return true;
  case NG_MSG_DRAW:
    mod_console_draw(ctx);
    return true;
  case NG_MSG_REPLY:
    if (msg->text) {
      mod_console_set_output(ctx, msg->text);
    }
    return true;
  default:
    return false;
  }
}

static bool mod_console_init(void *vctx) {
  ModConsoleCtx *ctx = (ModConsoleCtx *)vctx;
  memset(ctx, 0, sizeof(*ctx));
  ctx->open = false;
  ng_bus_set_gate(NG_BUS_RENDER, true);
  mod_console_set_output(ctx,
                         "Tab: toggle console\nscene sphere | scene cube");
  return true;
}

static void mod_console_shutdown(void *vctx) {
  (void)vctx;
}

static const NgModOps g_console_ops = {
    .name = "console",
    .dest = NG_BUS_CONSOLE,
    .init = mod_console_init,
    .shutdown = mod_console_shutdown,
    .on_msg = mod_console_on_msg,
};

const NgModOps *mod_console_ops(void) { return &g_console_ops; }

void *mod_console_ctx(void) { return &g_console_ctx; }
