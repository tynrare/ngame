// agent: composer-2.5 | 2026-07-25 | headless server orchestrator | j3m51h
// agent: composer-2.5 | 2026-07-28 | use shared server runtime | 776fad
// agent: composer-2.5 | 2026-08-02 | server ping loss throttle args | 45461d
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static bool g_ready = false;
static bool g_running = true;
static double g_last_time = 0.0;
static int g_throttle_pct = 0;

static double ng_server_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void ng_server_on_signal(int sig) {
  (void)sig;
  g_running = false;
}

static int ng_server_clamp_pct(int v) {
  if (v < 0) {
    return 0;
  }
  if (v > 100) {
    return 100;
  }
  return v;
}

static int ng_server_clamp_ping_ms(int v) {
  if (v < 0) {
    return 0;
  }
  if (v > 2000) {
    return 2000;
  }
  return v;
}

void ng_app_server_init(int argc, char **argv) {
  uint16_t port = NG_NET_DEFAULT_PORT;
  int ping_ms = 0;
  int loss_pct = 0;
  g_throttle_pct = 0;

  for (int i = 1; i < argc; i++) {
    if ((strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)) {
      fprintf(stderr,
              "Usage: ngame_server [options]\n"
              "  --port PORT        game port (default %u)\n"
              "  --ping MS          simulated one-way latency (ms)\n"
              "  --loss PCT         unreliable LOCK_INPUT loss percent (0..100)\n"
              "  --throttle PCT     FPS drop percent from 60Hz (0..100)\n",
              (unsigned)NG_NET_DEFAULT_PORT);
      g_running = false;
      return;
    }
    if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      port = (uint16_t)atoi(argv[++i]);
    } else if (strcmp(argv[i], "--ping") == 0 && i + 1 < argc) {
      ping_ms = ng_server_clamp_ping_ms(atoi(argv[++i]));
    } else if (strcmp(argv[i], "--loss") == 0 && i + 1 < argc) {
      loss_pct = ng_server_clamp_pct(atoi(argv[++i]));
    } else if (strcmp(argv[i], "--throttle") == 0 && i + 1 < argc) {
      g_throttle_pct = ng_server_clamp_pct(atoi(argv[++i]));
    }
  }

  signal(SIGINT, ng_server_on_signal);
  signal(SIGTERM, ng_server_on_signal);

  ng_bus_init();
  mod_net_configure(NULL, port);
  if (ping_ms > 0 || loss_pct > 0) {
    mod_net_sim_configure(ping_ms, loss_pct);
  }

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
  if (g_throttle_pct > 0) {
    const int fps = 60 * (100 - g_throttle_pct) / 100;
    NG_LOG_INFO("server ready (throttle=%d%% → ~%d Hz)", g_throttle_pct, fps > 0 ? fps : 1);
  } else {
    NG_LOG_INFO("server ready");
  }
}

void ng_app_server_frame(void) {
  if (!g_ready) {
    return;
  }

  const double frame_t0 = ng_server_now();
  ng_server_runtime_poll_net();
  ng_server_runtime_poll_agent();

  const double now = ng_server_now();
  const float dt = (float)(now - g_last_time);
  g_last_time = now;
  ng_server_runtime_frame(dt);
  // agent: composer-2.5 | 2026-07-30 | server publish_tick for lockstep | d59def
  ng_mod_publish_tick(dt);

  if (g_throttle_pct > 0) {
    int fps = 60 * (100 - g_throttle_pct) / 100;
    if (fps < 1) {
      fps = 1;
    }
    const double target = 1.0 / (double)fps;
    const double spent = ng_server_now() - frame_t0;
    if (spent < target) {
      const double rem = target - spent;
      struct timespec ts;
      ts.tv_sec = (time_t)rem;
      ts.tv_nsec = (long)((rem - (double)ts.tv_sec) * 1e9);
      if (ts.tv_nsec < 0) {
        ts.tv_nsec = 0;
      }
      nanosleep(&ts, NULL);
    }
  }
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
// agent: composer-2.5 | 2026-08-02 | server ping loss throttle args | 45461d
