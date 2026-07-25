// agent: composer-2.5 | 2026-07-25 | client app orchestrator | i2l40g
#include "ng_app_client.h"
#include "core/ng_bus.h"
#include "core/ng_log.h"
#include "core/ng_mod.h"
#include "mod/mod_console.h"
#include "mod/mod_net.h"
#include "mod/mod_render.h"
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

void ng_app_client_init(int argc, char **argv) {
  static char host_buf[64];
  const char *host = NG_NET_HOST;
  uint16_t port = NG_NET_DEFAULT_PORT;
#if defined(__EMSCRIPTEN__)
  port = NG_NET_WS_PORT;
  // agent: composer-2.5 | 2026-07-25 | web hostname from location | 518dc1
  EM_ASM({
    var h = (typeof location !== 'undefined' && location.hostname) ? location.hostname : '127.0.0.1';
    stringToUTF8(h, $0, 64);
  }, host_buf);
  host = host_buf;
#endif

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
      const char *spec = argv[++i];
      char spec_buf[128];
      strncpy(spec_buf, spec, sizeof(spec_buf) - 1);
      spec_buf[sizeof(spec_buf) - 1] = '\0';
      char *colon = strchr(spec_buf, ':');
      if (colon) {
        *colon = '\0';
        host = spec_buf;
        port = (uint16_t)atoi(colon + 1);
      } else {
        host = spec_buf;
      }
    }
  }

  ng_viewport_init(GetScreenWidth(), GetScreenHeight());
  ng_bus_init();
  mod_net_configure(host, port);

  ng_mod_register(mod_render_ops(), mod_render_ctx());
  ng_mod_register(mod_net_ops(), mod_net_ctx());
  ng_mod_register(mod_console_ops(), mod_console_ctx());

  if (!ng_mod_init_all()) {
    NG_LOG_ERROR("client module init failed");
    return;
  }

  // agent: composer-2.5 | 2026-07-25 | bootstrap until snapshot | e4d91c
  for (int i = 0; i < 1000; i++) {
    NgMsg tick = {
        .kind = NG_MSG_TICK,
        .from = NG_BUS_ANY,
        .to = NG_BUS_ANY,
        .dt = 0.016f,
    };
    ng_bus_publish(&tick);
    if (mod_render_has_snapshot()) {
      break;
    }
    usleep(10000);
  }

  g_ready = true;
}

void ng_app_client_frame(void) {
  if (!g_ready) {
    BeginDrawing();
    ClearBackground(BLACK);
    DrawText("NG: client init failed", 20, 20, 20, RED);
    EndDrawing();
    return;
  }

  ng_viewport_poll();
  ng_shader_poll();
  ng_mod_publish_tick(GetFrameTime());

  BeginDrawing();
  ng_mod_publish_draw();
  EndDrawing();
}

void ng_app_client_shutdown(void) {
  NgMsg msg = {
      .kind = NG_MSG_SHUTDOWN,
      .from = NG_BUS_ANY,
      .to = NG_BUS_ANY,
  };
  ng_bus_publish(&msg);
  ng_mod_shutdown_all();
  ng_bus_shutdown();
  g_ready = false;
}
