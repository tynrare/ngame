// agent: composer-2.5 | 2026-07-26 | duktape cube js smoke | 26a299
#include "core/ng_session.h"
#include "mod/mod_input.h"
#include "mod/mod_render.h"
#include "mod/mod_scene.h"
#include <stdio.h>
#include <stdlib.h>

void mod_render_apply_session(const NgSessionState *session) { (void)session; }
void mod_input_begin_frame(void) {}
int mod_input_buttons(void) { return 0; }
float mod_input_take_yaw(void) { return 0.0f; }

int main(void) {
  if (!mod_scene_smoke_test()) {
    fprintf(stderr, "scene_js_smoke: cube.js load/spawn failed\n");
    return 1;
  }
  printf("SCENE_JS_SMOKE ok\n");
  return 0;
}
