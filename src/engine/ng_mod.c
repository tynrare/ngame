// agent: composer-2.5 | 2026-07-25 | module registry lifecycle | 7d2f4b
// agent: composer-2.5 | 2026-07-29 | Extend NgModOps side fixed_step | 379437
// agent: composer-2.5 | 2026-07-29 | lockstep fixed tick gate | 39cc6d
#include "ng_mod.h"
#include "engine/ng_log.h"
#include <string.h>

#define NG_MOD_MAX 12

typedef struct NgModEntry {
  const NgModOps *ops;
  void *ctx;
} NgModEntry;

static NgModEntry g_mods[NG_MOD_MAX];
static int g_mod_count = 0;
// agent: composer-2.5 | 2026-07-29 | track last init failure | 0d3c9a
static const char *g_last_init_failed = NULL;
static float g_fixed_accum = 0.0f;
static uint32_t g_fixed_tick = 0;
static NgFixedGateFn g_fixed_gate = NULL;

void ng_mod_set_fixed_gate(NgFixedGateFn fn) { g_fixed_gate = fn; }

static NgModSide ng_mod_effective_side(NgModSide side) {
  return side == 0 ? NG_MOD_SIDE_BOTH : side;
}

static bool ng_mod_side_active(NgModSide side) {
  const NgModSide s = ng_mod_effective_side(side);
#if defined(NG_SERVER)
  return (s & NG_MOD_SIDE_SERVER) != 0;
#elif defined(NG_HAS_EMBEDDED)
  (void)s;
  return true;
#else
  return (s & NG_MOD_SIDE_CLIENT) != 0;
#endif
}

bool ng_mod_register(const NgModOps *ops, void *ctx) {
  if (!ops || g_mod_count >= NG_MOD_MAX) {
    return false;
  }
  if (!ng_mod_side_active(ops->side)) {
    return true;
  }
  g_mods[g_mod_count].ops = ops;
  g_mods[g_mod_count].ctx = ctx;
  g_mod_count++;
  ng_bus_subscribe(ops->dest, ops->on_msg, ctx);
  return true;
}

bool ng_mod_init_all(void) {
  for (int i = 0; i < g_mod_count; i++) {
    if (g_mods[i].ops->init && !g_mods[i].ops->init(g_mods[i].ctx)) {
      g_last_init_failed = g_mods[i].ops->name;
      NG_LOG_ERROR("mod init failed: %s", g_mods[i].ops->name);
      return false;
    }
  }
  g_last_init_failed = NULL;
  return true;
}

// agent: composer-2.5 | 2026-07-29 | last init failure accessor | 1a2b3c
const char *ng_mod_last_init_failed(void) { return g_last_init_failed; }

void ng_mod_shutdown_all(void) {
  for (int i = g_mod_count - 1; i >= 0; i--) {
    if (g_mods[i].ops->shutdown) {
      g_mods[i].ops->shutdown(g_mods[i].ctx);
    }
  }
  g_mod_count = 0;
  g_fixed_accum = 0.0f;
  g_fixed_tick = 0;
  g_fixed_gate = NULL;
}

void ng_mod_publish_tick(float dt) {
  float frame_dt = dt;
  if (frame_dt < 0.0f) {
    frame_dt = 0.0f;
  }
  if (frame_dt > 0.25f) {
    frame_dt = 0.25f;
  }
  g_fixed_accum += frame_dt;
  int steps = 0;
  while (g_fixed_accum >= NG_MOD_FIXED_DT && steps < NG_MOD_FIXED_MAX_STEPS) {
    NgFixedGate gate = NG_FIXED_GATE_GO;
    if (g_fixed_gate) {
      gate = g_fixed_gate();
    }
    if (gate == NG_FIXED_GATE_STALL) {
      break;
    }
    g_fixed_accum -= NG_MOD_FIXED_DT;
    steps++;
    if (gate == NG_FIXED_GATE_BUFFER) {
      continue;
    }
    g_fixed_tick++;
    for (int i = 0; i < g_mod_count; i++) {
      const NgModOps *ops = g_mods[i].ops;
      if (ops->fixed_step && ng_mod_side_active(ops->side)) {
        ops->fixed_step(g_mods[i].ctx, NG_MOD_FIXED_DT, g_fixed_tick);
      }
    }
  }
  if (steps == NG_MOD_FIXED_MAX_STEPS && g_fixed_accum >= NG_MOD_FIXED_DT) {
    g_fixed_accum = 0.0f;
  }

  NgMsg msg = {
      .kind = NG_MSG_TICK,
      .from = NG_BUS_ANY,
      .to = NG_BUS_ANY,
      .dt = dt,
  };
  ng_bus_publish(&msg);
}

void ng_mod_publish_draw(void) {
  NgMsg render_draw = {
      .kind = NG_MSG_DRAW,
      .from = NG_BUS_ANY,
      .to = NG_BUS_RENDER,
  };
  ng_bus_publish(&render_draw);
  NgMsg console_draw = {
      .kind = NG_MSG_DRAW,
      .from = NG_BUS_ANY,
      .to = NG_BUS_CONSOLE,
  };
  ng_bus_publish(&console_draw);
}

// agent: composer-2.5 | 2026-07-29 | track last init failure | 0d3c9a
// agent: composer-2.5 | 2026-07-29 | last init failure accessor | 1a2b3c
// agent: composer-2.5 | 2026-07-29 | Extend NgModOps side fixed_step | 379437
// agent: composer-2.5 | 2026-07-29 | lockstep fixed tick gate | 39cc6d
