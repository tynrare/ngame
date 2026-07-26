// agent: composer-2.5 | 2026-07-25 | client launch mode wiring | 3dd2a4
#include "ng_app_client.h"
#include "client/ng_app_embedded.h"
#include "core/ng_bus.h"
#include "core/ng_launch.h"
#include "core/ng_log.h"
#include "core/ng_mod.h"
#include "mod/mod_console.h"
#include "mod/mod_input.h"
#include "mod/mod_net.h"
#include "mod/mod_render.h"
#include "mod/mod_scene.h"
#include "net/ng_net.h"
#include "ng_shader.h"
#include "ng_viewport.h"
#include <raylib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

static bool g_ready = false;
static bool g_embedded = false;
static NgLaunchConfig g_launch = {0};

static bool ng_app_client_bootstrap(void) {
  const double deadline = GetTime() + 5.0;
  while (GetTime() < deadline) {
    NgMsg tick = {
        .kind = NG_MSG_TICK,
        .from = NG_BUS_ANY,
        .to = NG_BUS_ANY,
        .dt = 0.016f,
    };
    ng_bus_publish(&tick);
    mod_net_poll_recv();
    if (mod_render_has_snapshot()) {
      return true;
    }
    usleep(1000);
  }
  return false;
}

void ng_app_client_init(int argc, char **argv) {
  if (!ng_launch_parse(argc, argv, &g_launch)) {
    return;
  }
  g_embedded = g_launch.mode == NG_LAUNCH_EMBEDDED;

  if (g_launch.mode == NG_LAUNCH_REMOTE) {
    mod_net_configure(g_launch.host, g_launch.port);
  } else if (g_launch.mode == NG_LAUNCH_LOCAL) {
    mod_net_configure(NG_NET_HOST, g_launch.port);
  }

  InitWindow(800, 450, "ngame");
  SetWindowState(FLAG_WINDOW_RESIZABLE);
  SetTargetFPS(60);

#if defined(__EMSCRIPTEN__)
  // agent: composer-2.5 | 2026-07-25 | web resize after InitWindow | b7c3d9
  EM_ASM(window.dispatchEvent(new Event('resize')););
#endif
  ng_viewport_init(GetScreenWidth(), GetScreenHeight());
  ng_viewport_poll();

  if (g_embedded) {
    ng_app_embedded_init();
    g_ready = ng_app_embedded_ready();
    if (!g_ready) {
      NG_LOG_ERROR("embedded init failed");
    }
    return;
  }

  ng_viewport_init(GetScreenWidth(), GetScreenHeight());
  ng_bus_init();

  // agent: composer-2.5 | 2026-07-26 | restore local spawn UDP | 53fe75
#if !defined(__EMSCRIPTEN__)
  if (g_launch.mode == NG_LAUNCH_LOCAL) {
    if (!ng_launch_spawn_server(g_launch.port)) {
      NG_LOG_ERROR("failed to spawn ngame_server");
      return;
    }
  }
#endif

  ng_mod_register(mod_console_ops(), mod_console_ctx());
  ng_mod_register(mod_net_ops(), mod_net_ctx());
  ng_mod_register(mod_render_ops(), mod_render_ctx());
  ng_mod_register(mod_scene_ops(), mod_scene_ctx());

  if (!ng_mod_init_all()) {
    NG_LOG_ERROR("client module init failed");
    return;
  }

  g_ready = true;
  if (!ng_app_client_bootstrap()) {
    NG_LOG_WARN("client bootstrap: no snapshot yet");
  }
}

void ng_app_client_frame(void) {
  if (g_embedded) {
    if (!g_ready) {
      BeginDrawing();
      ClearBackground(BLACK);
      DrawText("NG: embedded init failed", 20, 20, 20, RED);
      EndDrawing();
      return;
    }
    ng_app_embedded_frame(GetFrameTime());
    return;
  }

  if (!g_ready) {
    BeginDrawing();
    ClearBackground(BLACK);
    DrawText("NG: client init failed", 20, 20, 20, RED);
    EndDrawing();
    return;
  }

  ng_viewport_poll();
  ng_shader_poll();
  mod_input_begin_frame();
  ng_mod_publish_tick(GetFrameTime());
  mod_net_poll_recv();

  BeginDrawing();
  ng_mod_publish_draw();
  EndDrawing();
}

void ng_app_client_shutdown(void) {
  if (g_embedded) {
    ng_app_embedded_shutdown();
  } else {
    NgMsg msg = {
        .kind = NG_MSG_SHUTDOWN,
        .from = NG_BUS_ANY,
        .to = NG_BUS_ANY,
    };
    ng_bus_publish(&msg);
    ng_mod_shutdown_all();
    ng_bus_shutdown();
    if (ng_launch_server_spawned()) {
      ng_launch_stop_server();
    }
  }
  g_ready = false;
}
