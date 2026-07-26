// agent: composer-2.5 | 2026-07-26 | session scene mode helper | d4e5f6
#include "ng_session.h"
#include <string.h>

bool ng_scene_client_fields(const char *scene_id) {
  return scene_id && strcmp(scene_id, "cube") == 0;
}
