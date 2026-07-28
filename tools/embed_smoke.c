// agent: composer-2.5 | 2026-07-25 | headless embedded smoke test | 69db60
// agent: composer-2.5 | 2026-07-28 | embedded cube scene load check | 2eb821
#include "engine/ng_bus.h"
#include "engine/ng_embed.h"
#include "engine/ng_log.h"
#include "engine/ng_mod.h"
#include "net/mod_net.h"
#include "scene/scene.h"
#include "server/sim.h"
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
  ng_mod_register(mod_scene_ops(), mod_scene_ctx());
  ng_mod_register(mod_sim_ops(), mod_sim_ctx());

  if (!ng_mod_init_all()) {
    fprintf(stderr, "embed_smoke: module init failed\n");
    CloseWindow();
    return 1;
  }

  if (!ng_embed_ready()) {
    fprintf(stderr, "embed_smoke: scene not ready\n");
    ng_mod_shutdown_all();
    ng_bus_shutdown();
    CloseWindow();
    return 1;
  }

  const char *argv[] = {"scene", "cube"};
  NgMsg cmd = {
      .kind = NG_MSG_CMD,
      .from = NG_BUS_CONSOLE,
      .to = NG_BUS_SIM,
      .argc = 2,
      .argv = argv,
  };
  char reply[256];
  if (!mod_sim_run_cmd(&cmd, reply, sizeof(reply))) {
    fprintf(stderr, "embed_smoke: scene cube cmd failed\n");
    ng_mod_shutdown_all();
    ng_bus_shutdown();
    CloseWindow();
    return 1;
  }
  if (strcmp(mod_scene_current_id(), "cube") != 0) {
    fprintf(stderr, "embed_smoke: expected cube got %s\n", mod_scene_current_id());
    ng_mod_shutdown_all();
    ng_bus_shutdown();
    CloseWindow();
    return 1;
  }

  printf("EMBED_SNAPSHOT scene=%s entities=%d\n", mod_scene_current_id(), mod_scene_entity_count());

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

// agent: composer-2.5 | 2026-07-28 | embedded cube scene load check | 2eb821
