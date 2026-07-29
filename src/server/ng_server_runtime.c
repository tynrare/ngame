// agent: composer-2.5 | 2026-07-28 | shared server tick runtime | 776fad
#include "ng_server_runtime.h"
#include "engine/ng_bus.h"
#include "server/agent.h"
#include "net/mod_net.h"

#define NG_SIM_HZ 60.0f
#define NG_SIM_STEP (1.0f / NG_SIM_HZ)
#define NG_SIM_MAX_STEPS 4

static float g_accum = 0.0f;

void ng_server_runtime_init(void) { g_accum = 0.0f; }

void ng_server_runtime_poll_net(void) {
#if defined(NG_SERVER)
  for (int i = 0; i < 8; i++) {
    mod_net_server_poll();
  }
#elif defined(NG_HAS_EMBEDDED)
  mod_net_gateway_host_poll();
#endif
}

void ng_server_runtime_poll_agent(void) { mod_agent_poll(); }

void ng_server_runtime_frame(float dt) {
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

void ng_server_runtime_shutdown(void) { g_accum = 0.0f; }

// agent: composer-2.5 | 2026-07-28 | shared server tick runtime | 776fad
