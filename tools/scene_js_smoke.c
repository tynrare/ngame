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

// agent: gpt-6-astra | 2026-09-05 | run shared drawing contract checks | 48dc0c
/** @return bool shared drawing API smoke result. */
bool ng_draw_smoke_test(void);

/** @return int nonzero on scene or drawing regression. */
int main(void) {
// agent: gpt-6-astra | 2026-09-05 | verify drawing before scene smoke suite | b8c704
  ng_world_init(&g_smoke_world);
  if (!ng_draw_smoke_test()) return 1;
  if (!mod_scene_smoke_test()) {
    fprintf(stderr, "scene_js_smoke: cube/sphere js load/spawn failed\n");
    return 1;
  }
  printf("SCENE_JS_SMOKE ok\n");
  return 0;
}
// agent: gpt-6-astra | 2026-09-05 | run shared drawing contract checks | 48dc0c
// agent: gpt-6-astra | 2026-09-05 | verify drawing before scene smoke suite | b8c704
