// agent: composer-2.5 | 2026-07-27 | js scene smoke stubs | 8b4c2d
#include "scene/scene.h"
#include "server/sim.h"
#include "world/ng_world.h"
#include <stdio.h>
#include <stdlib.h>

static NgWorld g_smoke_world;

NgWorld *mod_sim_world(void) { return &g_smoke_world; }

// This isolated scene test has no simulation command router.
bool mod_sim_load_scene(const char *id, char *reply, size_t reply_cap) {
  (void)id;
  if (reply && reply_cap) reply[0] = '\0';
  return false;
}

void mod_net_flush_scene_updates(void) {}

int main(void) {
  ng_world_init(&g_smoke_world);
  if (!mod_scene_smoke_test()) {
    fprintf(stderr, "scene_js_smoke: cube/sphere js load/spawn failed\n");
    return 1;
  }
  printf("SCENE_JS_SMOKE ok\n");
  return 0;
}
