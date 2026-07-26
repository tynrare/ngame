// agent: composer-2.5 | 2026-07-25 | embedded app orchestrator | 680bc7
#include "ng_app_embedded.h"
#include "core/ng_bus.h"
#include "core/ng_embed.h"
#include "core/ng_log.h"
#include "core/ng_mod.h"
#include "mod/mod_agent.h"
#include "mod/mod_console.h"
#include "mod/mod_input.h"
#include "mod/mod_net.h"
#include "mod/mod_render.h"
#include "mod/mod_script.h"
#include "mod/mod_sim.h"
#include "ng_shader.h"
#include "ng_viewport.h"
#include <raylib.h>

static bool g_ready = false;

void ng_app_embedded_init(void) {
  ng_bus_init();
  mod_net_set_embedded(true);

  ng_mod_register(mod_net_ops(), mod_net_ctx());
  ng_mod_register(mod_script_ops(), mod_script_ctx());
  ng_mod_register(mod_sim_ops(), mod_sim_ctx());
#ifndef __EMSCRIPTEN__
  ng_mod_register(mod_agent_ops(), mod_agent_ctx());
#endif
  ng_mod_register(mod_render_ops(), mod_render_ctx());
  ng_mod_register(mod_console_ops(), mod_console_ctx());

  if (!ng_mod_init_all()) {
    NG_LOG_ERROR("embedded module init failed");
    return;
  }

  if (!ng_embed_ready()) {
    NG_LOG_ERROR("embedded bootstrap: world not ready");
    return;
  }

  ng_viewport_poll();
  g_ready = true;
  NG_LOG_INFO("embedded ready");
}

// agent: composer-2.5 | 2026-07-25 | clean client hot path frame | d5e8a1
void ng_app_embedded_frame(float dt) {
  if (!g_ready) {
    return;
  }

  ng_viewport_poll();
  ng_shader_poll();
  mod_input_begin_frame();

  NgMsg tick = {
      .kind = NG_MSG_TICK,
      .from = NG_BUS_ANY,
      .to = NG_BUS_ANY,
      .dt = dt,
  };
  ng_bus_publish(&tick);

  BeginDrawing();
  ng_mod_publish_draw();
  EndDrawing();
}

void ng_app_embedded_shutdown(void) {
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

bool ng_app_embedded_ready(void) { return g_ready; }
