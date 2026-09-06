// agent: composer-2.5 | 2026-07-25 | headless embedded smoke test | 69db60
// agent: composer-2.5 | 2026-07-28 | gateway loopback smoke test | 2eb821
// agent: grok-4.6 | 2026-08-31 | link font_msdf smoke bins | dfc12a
#include "engine/ng_bus.h"
#include "engine/ng_log.h"
#include "engine/ng_mod.h"
#include "net/mod_net.h"
#include "scene/scene.h"
#include "server/sim.h"
#include "server/script.h"
#include <raylib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 2abee8
extern bool ng_shape_smoke_test(void);

int main(void) {
  SetTraceLogLevel(LOG_NONE);
  InitWindow(8, 8, "embed_smoke");
  SetTargetFPS(0);

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 2abee8
  if(!ng_shape_smoke_test()) { CloseWindow(); return 1; }
  ng_bus_init();
  mod_net_set_gateway(true);
  ng_mod_register(mod_net_ops(), mod_net_ctx());
  ng_mod_register(mod_scene_ops(), mod_scene_ctx());
  ng_mod_register(mod_script_ops(), mod_script_ctx());
  ng_mod_register(mod_sim_ops(), mod_sim_ctx());

  if (!ng_mod_init_all()) {
    fprintf(stderr, "embed_smoke: module init failed\n");
    CloseWindow();
    return 1;
  }

  for (int i = 0; i < 8; i++) {
    mod_net_gateway_host_poll();
  }

  if (!mod_scene_is_loaded()) {
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

// agent: grok-4.6 | 2026-08-31 | link font_msdf smoke bins | dfc12a
uint16_t mod_agent_listening_port(void) { return 0; }
const char *ng_app_client_mode_text(void) { return "embedded"; }
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 2abee8

// agent: composer-2.5 | 2026-07-25 | headless embedded smoke test | 69db60
// agent: composer-2.5 | 2026-07-28 | gateway loopback smoke test | 2eb821
// agent: grok-4.6 | 2026-08-31 | link font_msdf smoke bins | dfc12a
