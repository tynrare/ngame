// agent: composer-2.5 | 2026-07-25 | headless server orchestrator | j3m51h
// agent: composer-2.5 | 2026-07-28 | use shared server runtime | 776fad
#include "ng_app_server.h"
#include "engine/ng_bus.h"
#include "engine/ng_log.h"
#include "engine/ng_mod.h"
#include "server/agent.h"
#include "net/mod_net.h"
#include "scene/scene.h"
#include "server/ng_server_runtime.h"
#include "server/script.h"
#include "server/sim.h"
#include "net/ng_net.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static bool g_ready = false;
static bool g_running = true;
static double g_last_time = 0.0;

static double ng_server_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void ng_server_on_signal(int sig) {
  (void)sig;
  g_running = false;
}

void ng_app_server_init(int argc, char **argv) {
  uint16_t port = NG_NET_DEFAULT_PORT;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      port = (uint16_t)atoi(argv[++i]);
    }
  }

  signal(SIGINT, ng_server_on_signal);
  signal(SIGTERM, ng_server_on_signal);

  ng_bus_init();
  mod_net_configure(NULL, port);

  ng_mod_register(mod_net_ops(), mod_net_ctx());
  ng_mod_register(mod_script_ops(), mod_script_ctx());
  ng_mod_register(mod_scene_ops(), mod_scene_ctx());
  ng_mod_register(mod_sim_ops(), mod_sim_ctx());
  ng_mod_register(mod_agent_ops(), mod_agent_ctx());

  if (!ng_mod_init_all()) {
    NG_LOG_ERROR("server module init failed");
    return;
  }

  ng_server_runtime_init();
  g_last_time = ng_server_now();
  g_ready = true;
  NG_LOG_INFO("server ready");
}

void ng_app_server_frame(void) {
  if (!g_ready) {
    return;
  }

  ng_server_runtime_poll_net();
  ng_server_runtime_poll_agent();

  const double now = ng_server_now();
  const float dt = (float)(now - g_last_time);
  g_last_time = now;
  ng_server_runtime_frame(dt);
  // agent: composer-2.5 | 2026-07-30 | server publish_tick for lockstep | d59def
  ng_mod_publish_tick(dt);
}

void ng_app_server_shutdown(void) {
  NgMsg msg = {
      .kind = NG_MSG_SHUTDOWN,
      .from = NG_BUS_ANY,
      .to = NG_BUS_ANY,
  };
  ng_bus_publish(&msg);
  ng_server_runtime_shutdown();
  ng_mod_shutdown_all();
  ng_bus_shutdown();
  ng_net_shutdown();
  g_ready = false;
}

bool ng_app_server_running(void) { return g_running; }

// agent: composer-2.5 | 2026-07-28 | use shared server runtime | 776fad
// agent: composer-2.5 | 2026-07-30 | server publish_tick for lockstep | d59def
