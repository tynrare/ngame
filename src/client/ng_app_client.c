// agent: composer-2.5 | 2026-07-25 | client launch mode wiring | 3dd2a4
// agent: composer-2.5 | 2026-07-28 | gateway client orchestrator | 0e2e03
// agent: composer-2.5 | 2026-07-29 | console early register order | 935cbc
// agent: composer-2.5 | 2026-07-29 | poll console after BeginDrawing | a4b5c6
#include "ng_app_client.h"
#include "engine/ng_bus.h"
#include "engine/ng_launch.h"
#include "engine/ng_log.h"
#include "engine/ng_mod.h"
#include "client/console.h"
#include "client/input.h"
#include "net/mod_net.h"
#include "client/render.h"
#include "scene/scene.h"
#include "server/agent.h"
#include "server/script.h"
#include "server/sim.h"
#include "server/ng_server_runtime.h"
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
static NgLaunchConfig g_launch = {0};
// agent: composer-2.5 | 2026-07-29 | verbose init failure reporting | c3d179
static char g_init_error[256] = {0};

static bool ng_app_client_bootstrap(void) {
  const double deadline = GetTime() + 8.0;
  while (GetTime() < deadline) {
    ng_server_runtime_poll_net();
    mod_net_poll_recv();
    NgMsg tick = {
        .kind = NG_MSG_TICK,
        .from = NG_BUS_ANY,
        .to = NG_BUS_ANY,
        .dt = 0.016f,
    };
    ng_bus_publish(&tick);
    if (g_launch.use_upstream) {
      if (mod_scene_view_is_loaded()) {
        return true;
      }
    } else if (mod_render_has_snapshot() || mod_scene_view_is_loaded()) {
      return true;
    }
    usleep(1000);
  }
  return false;
}

// agent: composer-2.5 | 2026-07-29 | expose launch mode text | c7835e
const char *ng_app_client_mode_text(void) {
  switch (g_launch.mode) {
  case NG_LAUNCH_LOCAL:
    return "local";
  case NG_LAUNCH_REMOTE:
    return "remote";
  case NG_LAUNCH_SOLO:
    return "embedded";
  default:
    return "unknown";
  }
}

void ng_app_client_init(int argc, char **argv) {
  if (!ng_launch_parse(argc, argv, &g_launch)) {
    return;
  }

  InitWindow(800, 450, "ngame");
  SetWindowState(FLAG_WINDOW_RESIZABLE);
  SetTargetFPS(60);

#if defined(__EMSCRIPTEN__)
  EM_ASM(window.dispatchEvent(new Event('resize')););
#endif
  ng_viewport_init(GetScreenWidth(), GetScreenHeight());
  ng_viewport_poll();

#if !defined(__EMSCRIPTEN__)
  if (g_launch.mode == NG_LAUNCH_LOCAL) {
    if (!ng_launch_spawn_server(g_launch.port)) {
      NG_LOG_ERROR("failed to spawn ngame_server");
      snprintf(g_init_error, sizeof(g_init_error), "spawn ngame_server failed port=%u", g_launch.port);
      return;
    }
  }
#endif

  mod_net_set_gateway(true);
  if (g_launch.use_upstream) {
    mod_net_configure_upstream(g_launch.host, g_launch.port);
  } else {
    mod_agent_configure(g_launch.agent_port);
  }

  ng_bus_init();

  // agent: composer-2.5 | 2026-07-29 | scene init before net upstream | f1a3c8
  ng_mod_register(mod_scene_ops(), mod_scene_ctx());
  ng_mod_register(mod_sim_ops(), mod_sim_ctx());
  ng_mod_register(mod_net_ops(), mod_net_ctx());
  ng_mod_register(mod_console_ops(), mod_console_ctx());
  ng_mod_register(mod_script_ops(), mod_script_ctx());
  ng_mod_register(mod_agent_ops(), mod_agent_ctx());
  ng_mod_register(mod_render_ops(), mod_render_ctx());

  if (!ng_mod_init_all()) {
    const char *failed = ng_mod_last_init_failed();
    NG_LOG_ERROR("gateway client init failed mod=%s server_port=%u upstream=%s agent_port=%u",
                 failed ? failed : "?", g_launch.port,
                 g_launch.use_upstream ? "yes" : "no", g_launch.agent_port);
    snprintf(g_init_error, sizeof(g_init_error), "init failed mod=%s server_port=%u upstream=%d agent_port=%u",
             failed ? failed : "?", g_launch.port, g_launch.use_upstream ? 1 : 0, g_launch.agent_port);
    return;
  }

  if (g_launch.use_upstream) {
    // agent: composer-2.5 | 2026-07-30 | remote connect no freeze | c52f96
    /* One non-blocking drain; connect/register/world continue in the frame loop. */
    mod_net_gateway_sync_view();
    NG_LOG_INFO("Remote mode: drawing while we connect — watch the status line.");
  }

  mod_net_gateway_resync();
  ng_server_runtime_init();
  g_ready = true;
  // agent: composer-2.5 | 2026-07-29 | log actual agent listen | cce4f2
  const uint16_t listen_port = mod_agent_listening_port();
#if defined(NG_HAS_EMBEDDED)
  const uint16_t assigned_port = mod_net_assigned_agent_port();
#else
  const uint16_t assigned_port = 0;
#endif
  if (g_launch.use_upstream) {
    NG_LOG_INFO("Gateway is up. Joining %s:%u as a remote client...", g_launch.host,
                g_launch.port);
  } else {
    NG_LOG_INFO("Gateway ready (offline/solo). agent listen=%u", listen_port);
  }
  (void)assigned_port;

  if (!g_launch.use_upstream) {
    if (!ng_app_client_bootstrap()) {
      NG_LOG_WARN("Local world not ready yet — will keep trying in-frame.");
    }
  }
}

void ng_app_client_frame(void) {
  if (!g_ready) {
    BeginDrawing();
    ClearBackground(BLACK);
    DrawText("NG: gateway init failed", 20, 20, 20, RED);
    if (g_init_error[0] != '\0') {
      DrawText(g_init_error, 20, 45, 16, RAYWHITE);
    }
    EndDrawing();
    return;
  }

  const float dt = GetFrameTime();

  ng_server_runtime_poll_net();
  ng_server_runtime_poll_agent();
  ng_server_runtime_frame(dt);

  ng_viewport_poll();
  ng_shader_poll();
  mod_input_begin_frame();
  mod_net_poll_recv();
  ng_mod_publish_tick(dt);

  BeginDrawing();
  mod_console_poll_input();
  ng_mod_publish_draw();
  EndDrawing();
}

void ng_app_client_shutdown(void) {
  if (g_ready) {
    NgMsg msg = {
        .kind = NG_MSG_SHUTDOWN,
        .from = NG_BUS_ANY,
        .to = NG_BUS_ANY,
    };
    ng_bus_publish(&msg);
    ng_server_runtime_shutdown();
    ng_mod_shutdown_all();
    ng_bus_shutdown();
  }
  if (ng_launch_server_spawned()) {
    ng_launch_stop_server();
  }
  g_ready = false;
}

// agent: composer-2.5 | 2026-07-28 | gateway client orchestrator | 0e2e03
// agent: composer-2.5 | 2026-07-29 | verbose init failure reporting | c3d179
// agent: composer-2.5 | 2026-07-29 | log actual agent listen | cce4f2
// agent: composer-2.5 | 2026-07-29 | console early register order | 935cbc
// agent: composer-2.5 | 2026-07-29 | poll console after BeginDrawing | a4b5c6
// agent: composer-2.5 | 2026-07-29 | scene init before net upstream | eda89a
// agent: composer-2.5 | 2026-07-29 | expose launch mode text | c7835e
// agent: composer-2.5 | 2026-07-30 | remote connect no freeze | c52f96
