// agent: composer-2.5 | 2026-07-25 | console bus module | 6b2e9a
// agent: composer-2.5 | 2026-07-29 | poll input after BeginDrawing | f8e1d0
#include "console.h"
#include "engine/ng_bus.h"
// agent: composer-2.5 | 2026-07-29 | explicit launch mode include | b5d2c1
#include "client/ng_app_client.h"
#include "net/mod_net.h"
#include "client/render.h"
#include "scene/scene.h"
#include "server/agent.h"
#include <raylib.h>
#include <string.h>
// agent: composer-2.5 | 2026-07-29 | console include stdio | b2c613
#include <stdio.h>

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

  // agent: composer-2.5 | 2026-07-29 | console ? status commands | 0b1c2d
  if (strcmp(ctx->last_cmd, "?") == 0) {
#if defined(NG_HAS_EMBEDDED) || !defined(NG_SERVER)
    mod_console_set_output(ctx, "? | status | mcp | scene <id>");
#else
    mod_console_set_output(ctx, "? | status | scene <id>");
#endif
    ctx->input[0] = '\0';
    ctx->input_len = 0;
    ctx->submitted = false;
    return;
  }

  // agent: composer-2.5 | 2026-07-29 | explicit launch mode in status | 19de42
  if (strcmp(ctx->last_cmd, "status") == 0 || strcmp(ctx->last_cmd, "mcp") == 0) {
#if defined(NG_HAS_EMBEDDED) || !defined(NG_SERVER)
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
    char status[800] = {0};
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
             mod_net_upstream_connected() ? 1 : 0, listen_port, elapsed, root_line, view_line,
             vis_line, render_line);
    mod_console_set_output(ctx, status);
#else
    mod_console_set_output(ctx, "status n/a");
#endif
    ctx->input[0] = '\0';
    ctx->input_len = 0;
    ctx->submitted = false;
    return;
  }

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
  // agent: composer-2.5 | 2026-07-29 | only show requested output | 91af2c
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
  // agent: composer-2.5 | 2026-07-29 | console init clears output | 5e6f7a
  mod_console_set_output(ctx, "");
  return true;
}

static void mod_console_shutdown(void *vctx) {
  (void)vctx;
}

// agent: composer-2.5 | 2026-07-29 | Extend NgModOps side fixed_step | 283fa4
static const NgModOps g_console_ops = {
    .name = "console",
    .dest = NG_BUS_CONSOLE,
    .side = NG_MOD_SIDE_CLIENT,
    .init = mod_console_init,
    .shutdown = mod_console_shutdown,
    .on_msg = mod_console_on_msg,
    .fixed_step = NULL,
};

const NgModOps *mod_console_ops(void) { return &g_console_ops; }

void *mod_console_ctx(void) { return &g_console_ctx; }

void mod_console_poll_input(void) {
  ModConsoleCtx *ctx = &g_console_ctx;
  mod_console_update_input(ctx);
  mod_console_submit(ctx);
}

// agent: composer-2.5 | 2026-07-29 | console verbose status | 87ab0b
// agent: composer-2.5 | 2026-07-29 | console ? status commands | 0b1c2d
// agent: composer-2.5 | 2026-07-29 | explicit launch mode include | b5d2c1
// agent: composer-2.5 | 2026-07-29 | explicit launch mode in status | 19de42
// agent: composer-2.5 | 2026-07-29 | only show requested output | 91af2c
// agent: composer-2.5 | 2026-07-29 | console init clears output | 5e6f7a
// agent: composer-2.5 | 2026-07-29 | console include stdio | b2c613
// agent: composer-2.5 | 2026-07-28 | generic scene console help | 0a3775
// agent: composer-2.5 | 2026-07-29 | poll input after BeginDrawing | f8e1d0
// agent: composer-2.5 | 2026-07-29 | Extend NgModOps side fixed_step | 283fa4
