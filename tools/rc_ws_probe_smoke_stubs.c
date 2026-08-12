/* agent: grok-4.6 | 2026-08-12 | rc_ws probe smoke tool | 585808 */
/** Minimal scene stubs so render_rc_ws.c links headless. */
#include "scene/assets.h"
#include "scene/graph.h"
#include <string.h>

int mod_scene_graph_inst_count(void) {
  return 0;
}

const NgSceneInst *mod_scene_graph_inst_at(int index) {
  (void)index;
  return NULL;
}

bool mod_scene_assets_resolve_model(const char *model_name, NgSceneResolvedModel *out) {
  (void)model_name;
  if (out) {
    memset(out, 0, sizeof(*out));
  }
  return false;
}
