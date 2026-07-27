// agent: composer-2.5 | 2026-07-25 | headless server orchestrator | j3m51h
#include "ng_app_server.h"
#include "core/ng_bus.h"
#include "core/ng_log.h"
#include "core/ng_mod.h"
#include "mod/mod_agent.h"
#include "mod/mod_net.h"
#include "mod/mod_scene.h"
#include "mod/mod_script.h"
#include "mod/mod_sim.h"
#include "net/ng_net.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NG_SIM_HZ 60.0f
#define NG_SIM_STEP (1.0f / NG_SIM_HZ)
#define NG_SIM_MAX_STEPS 4

static bool g_ready = false;
static bool g_running = true;
static double g_last_time = 0.0;
static float g_accum = 0.0f;

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

  g_last_time = ng_server_now();
  g_ready = true;
  NG_LOG_INFO("server ready");
}

void ng_app_server_frame(void) {
  if (!g_ready) {
    return;
  }

  // agent: composer-2.5 | 2026-07-26 | poll net before sim tick | f0a1b2
  for (int i = 0; i < 8; i++) {
    mod_net_server_poll();
  }
  mod_agent_poll();

  const double now = ng_server_now();
  const float dt = (float)(now - g_last_time);
  g_last_time = now;
  if (dt < 0.0f) {
    return;
  }

  NgMsg tick = {
      .kind = NG_MSG_TICK,
      .from = NG_BUS_ANY,
      .to = NG_BUS_ANY,
      .dt = dt,
  };
  ng_bus_publish(&tick);

  // agent: composer-2.5 | 2026-07-25 | cap sim substeps per frame | fcb318
  g_accum += dt;
  int sim_steps = 0;
  while (g_accum >= NG_SIM_STEP && sim_steps < NG_SIM_MAX_STEPS) {
    NgMsg sim_tick = {
        .kind = NG_MSG_TICK,
        .from = NG_BUS_ANY,
        .to = NG_BUS_SIM,
        .dt = NG_SIM_STEP,
    };
    ng_bus_publish(&sim_tick);
    g_accum -= NG_SIM_STEP;
    sim_steps++;
  }
}

void ng_app_server_shutdown(void) {
  NgMsg msg = {
      .kind = NG_MSG_SHUTDOWN,
      .from = NG_BUS_ANY,
      .to = NG_BUS_ANY,
  };
  ng_bus_publish(&msg);
  ng_mod_shutdown_all();
  ng_bus_shutdown();
  ng_net_shutdown();
  g_ready = false;
}

bool ng_app_server_running(void) { return g_running; }
