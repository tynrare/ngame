// agent: composer-2.5 | 2026-07-25 | headless embedded smoke test | 69db60
#include "core/ng_bus.h"
#include "core/ng_embed.h"
#include "core/ng_log.h"
#include "core/ng_mod.h"
#include "mod/mod_net.h"
#include "mod/mod_sim.h"
#include <raylib.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  SetTraceLogLevel(LOG_NONE);
  InitWindow(8, 8, "embed_smoke");
  SetTargetFPS(0);

  ng_bus_init();
  mod_net_set_embedded(true);
  ng_mod_register(mod_net_ops(), mod_net_ctx());
  ng_mod_register(mod_sim_ops(), mod_sim_ctx());

  if (!ng_mod_init_all()) {
    fprintf(stderr, "embed_smoke: module init failed\n");
    CloseWindow();
    return 1;
  }

  const NgWorld *w = ng_embed_world();
  if (!ng_embed_ready() || !w) {
    fprintf(stderr, "embed_smoke: world not ready\n");
    ng_mod_shutdown_all();
    ng_bus_shutdown();
    CloseWindow();
    return 1;
  }

  printf("EMBED_SNAPSHOT scene=%s\n", w->scene_id);

  NgMsg shutdown = {
      .kind = NG_MSG_SHUTDOWN,
      .from = NG_BUS_ANY,
      .to = NG_BUS_ANY,
  };
  ng_bus_publish(&shutdown);
  ng_mod_shutdown_all();
  ng_bus_shutdown();
  CloseWindow();
  return 0;
}
