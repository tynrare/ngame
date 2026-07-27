// agent: composer-2.5 | 2026-07-27 | js scene smoke stubs | 8b4c2d
#include "mod/mod_scene.h"
#include <stdio.h>
#include <stdlib.h>

void mod_net_flush_scene_updates(void) {}

int main(void) {
  if (!mod_scene_smoke_test()) {
    fprintf(stderr, "scene_js_smoke: cube/sphere js load/spawn failed\n");
    return 1;
  }
  printf("SCENE_JS_SMOKE ok\n");
  return 0;
}
