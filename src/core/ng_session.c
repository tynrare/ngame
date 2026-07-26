// agent: composer-2.5 | 2026-07-26 | session scene sync helper | d4e5f6
#include "ng_session.h"
#include <string.h>

NgSyncMode ng_scene_sync_mode(const char *scene_id) {
  if (scene_id && strcmp(scene_id, "cube") == 0) {
    return NG_SYNC_SHARED;
  }
  return NG_SYNC_SERVER;
}

bool ng_scene_has_js_host(const char *scene_id) {
  return ng_scene_sync_mode(scene_id) != NG_SYNC_SERVER;
}
