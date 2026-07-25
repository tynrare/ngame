// agent: composer-2.5 | 2026-07-25 | app lifecycle and frame | 4c1f8a
#include "ng_app.h"
#include "ng_cli.h"
#include "ng_console.h"
#include "ng_path.h"
#include "ng_scene.h"
#include "ng_scene_cube.h"
#include "ng_scene_sphere.h"
#include "ng_script.h"
#include "ng_shader.h"
#include "ng_viewport.h"
#include <raylib.h>
#include <string.h>

static NgScript g_script = {0};
static NgConsole g_console = {0};
static bool g_ready = false;

void ng_app_init(void) {
  ng_viewport_init(GetScreenWidth(), GetScreenHeight());
  ng_console_init(&g_console);

  if (!ng_script_init(&g_script)) {
    TraceLog(LOG_ERROR, "NG: script init failed");
    return;
  }
  if (!ng_script_run_file(&g_script, NG_RES_ROOT "cli.js")) {
    TraceLog(LOG_ERROR, "NG: cli.js load failed");
    return;
  }

  ng_scene_register(ng_scene_sphere_ops());
  ng_scene_register(ng_scene_cube_ops());
  ng_scene_load("sphere");
  g_ready = true;
}

void ng_app_cli_line(const char *line) {
  if (!line || !g_script.ctx) {
    ng_console_set_output(&g_console, "script not ready");
    return;
  }

  duk_get_global_string(g_script.ctx, "ng_cli_exec_line");
  if (!duk_is_function(g_script.ctx, -1)) {
    TraceLog(LOG_ERROR, "NG: ng_cli_exec_line not found in cli.js");
    ng_console_set_output(&g_console, "cli.js: ng_cli_exec_line missing");
    duk_pop(g_script.ctx);
    return;
  }
  duk_push_string(g_script.ctx, line);
  if (duk_pcall(g_script.ctx, 1) != DUK_EXEC_SUCCESS) {
    const char *err = duk_safe_to_string(g_script.ctx, -1);
    TraceLog(LOG_ERROR, "NG: cli error: %s", err);
    ng_console_set_output(&g_console, err);
    duk_pop(g_script.ctx);
    duk_pop(g_script.ctx);
    return;
  }
  duk_pop(g_script.ctx);

  const char *msg = ng_cli_last_output();
  if (msg && msg[0]) {
    ng_console_set_output(&g_console, msg);
  }
}

void ng_app_frame(void) {
  if (!g_ready) {
    BeginDrawing();
    ClearBackground(BLACK);
    DrawText("NG: init failed", 20, 20, 20, RED);
    EndDrawing();
    return;
  }

  ng_viewport_poll();
  ng_shader_poll();
  ng_console_update(&g_console);

  const char *line = ng_console_take_line(&g_console);
  if (line) {
    ng_app_cli_line(line);
  }

  if (!ng_console_blocks_scene(&g_console)) {
    ng_scene_update(GetFrameTime());
  }

  BeginDrawing();
  ng_scene_draw();
  ng_console_draw(&g_console);
  EndDrawing();
}

void ng_app_shutdown(void) {
  ng_scene_shutdown();
  ng_script_shutdown(&g_script);
  g_ready = false;
}
