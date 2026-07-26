// agent: composer-2.5 | 2026-07-26 | in-process loopback cmd RTT | b4c5d6
#include "core/ng_bus.h"
#include "core/ng_mod.h"
#include "mod/mod_net.h"
#include "mod/mod_sim.h"
#include <raylib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define NG_CMD_LATENCY_MAX_MS 100

static double ng_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static void ng_loopback_tick(void) {
  NgMsg tick = {
      .kind = NG_MSG_TICK,
      .from = NG_BUS_ANY,
      .to = NG_BUS_ANY,
      .dt = 0.001f,
  };
  ng_bus_publish(&tick);
  mod_net_poll_recv();
}

static void ng_run_cmd(const char *line) {
  NgMsg cmd = {
      .kind = NG_MSG_CMD,
      .from = NG_BUS_CONSOLE,
      .to = NG_BUS_NET,
      .line = line,
  };
  ng_bus_publish(&cmd);
}

int main(void) {
  SetTraceLogLevel(LOG_NONE);
  InitWindow(8, 8, "cmd_loopback_latency");
  SetTargetFPS(0);

  ng_bus_init();
  mod_net_set_local_loopback(true);
  ng_mod_register(mod_net_ops(), mod_net_ctx());
  ng_mod_register(mod_sim_ops(), mod_sim_ctx());

  if (!ng_mod_init_all()) {
    fprintf(stderr, "cmd_loopback_latency: init failed\n");
    CloseWindow();
    return 1;
  }

  if (!mod_net_is_connected()) {
    fprintf(stderr, "cmd_loopback_latency: loopback not connected\n");
    ng_mod_shutdown_all();
    ng_bus_shutdown();
    CloseWindow();
    return 1;
  }

  ng_run_cmd("scene cube");
  ng_run_cmd("scene sphere");
  for (int i = 0; i < 4; i++) {
    ng_loopback_tick();
  }

  const double t0 = ng_now_ms();
  ng_run_cmd("scene cube");
  const double ms = ng_now_ms() - t0;

  const NgWorld *w = mod_sim_world();
  if (!w || strcmp(w->scene_id, "cube") != 0) {
    fprintf(stderr, "cmd_loopback_latency: expected scene cube, got %s\n",
            w ? w->scene_id : "?");
    ng_mod_shutdown_all();
    ng_bus_shutdown();
    CloseWindow();
    return 1;
  }

  printf("CMD_LATENCY_MS %.3f\n", ms);
  if (ms > NG_CMD_LATENCY_MAX_MS) {
    fprintf(stderr, "cmd_loopback_latency: FAIL %.3f ms > %d ms\n", ms, NG_CMD_LATENCY_MAX_MS);
    ng_mod_shutdown_all();
    ng_bus_shutdown();
    CloseWindow();
    return 1;
  }

  ng_mod_shutdown_all();
  ng_bus_shutdown();
  CloseWindow();
  return 0;
}
