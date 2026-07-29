// agent: composer-2.5 | 2026-07-25 | module registry lifecycle | 7d2f4b
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

bool ng_mod_register(const NgModOps *ops, void *ctx) {
  if (!ops || g_mod_count >= NG_MOD_MAX) {
    return false;
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
}

void ng_mod_publish_tick(float dt) {
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
