// agent: composer-2.5 | 2026-07-25 | message bus router | 4a8c1e
#include "ng_bus.h"
#include "ng_log.h"
#include <string.h>

#define NG_BUS_SUBS_MAX 24

typedef struct NgBusSub {
  NgBusDest dest;
  NgBusHandler handler;
  void *ctx;
  bool used;
} NgBusSub;

static NgBusSub g_subs[NG_BUS_SUBS_MAX];
static bool g_gates[NG_BUS_DEST_MAX];

void ng_bus_init(void) {
  memset(g_subs, 0, sizeof(g_subs));
  for (int i = 0; i < NG_BUS_DEST_MAX; i++) {
    g_gates[i] = true;
  }
}

void ng_bus_shutdown(void) {
  memset(g_subs, 0, sizeof(g_subs));
}

NgBusDest ng_bus_dest_from_string(const char *name) {
  if (!name) {
    return NG_BUS_ANY;
  }
  if (strcmp(name, "console") == 0) {
    return NG_BUS_CONSOLE;
  }
  if (strcmp(name, "script") == 0) {
    return NG_BUS_SCRIPT;
  }
  if (strcmp(name, "scene") == 0 || strcmp(name, "sim") == 0) {
    return NG_BUS_SIM;
  }
  if (strcmp(name, "net") == 0) {
    return NG_BUS_NET;
  }
  if (strcmp(name, "render") == 0) {
    return NG_BUS_RENDER;
  }
  if (strcmp(name, "agent") == 0) {
    return NG_BUS_AGENT;
  }
  return NG_BUS_ANY;
}

void ng_bus_subscribe(NgBusDest dest, NgBusHandler handler, void *ctx) {
  for (int i = 0; i < NG_BUS_SUBS_MAX; i++) {
    if (!g_subs[i].used) {
      g_subs[i].dest = dest;
      g_subs[i].handler = handler;
      g_subs[i].ctx = ctx;
      g_subs[i].used = true;
      return;
    }
  }
  NG_LOG_ERROR("bus subscriber table full");
}

bool ng_bus_publish(NgMsg *msg) {
  if (!msg) {
    return false;
  }

  bool handled = false;
  for (int i = 0; i < NG_BUS_SUBS_MAX; i++) {
    if (!g_subs[i].used || !g_subs[i].handler) {
      continue;
    }
    const NgBusDest sub_dest = g_subs[i].dest;
    if (msg->to != NG_BUS_ANY && sub_dest != msg->to) {
      continue;
    }
    if (g_subs[i].handler(msg, g_subs[i].ctx)) {
      handled = true;
    }
  }
  return handled;
}

void ng_bus_set_gate(NgBusDest dest, bool enabled) {
  if (dest >= 0 && dest < NG_BUS_DEST_MAX) {
    g_gates[dest] = enabled;
  }
}

bool ng_bus_gate(NgBusDest dest) {
  if (dest < 0 || dest >= NG_BUS_DEST_MAX) {
    return true;
  }
  return g_gates[dest];
}
