// agent: composer-2.5 | 2026-07-26 | session and state update types | a1b2c3
#ifndef NG_SESSION_H
#define NG_SESSION_H

#include "ng_sync.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct NgSessionState {
  char scene_id[32];
  uint32_t tick;
  uint8_t controller_id;
  uint8_t your_id;
  uint32_t cube_entity_id;
  NgSyncMode scene_sync;
} NgSessionState;

typedef struct NgStateUpdate {
  uint32_t entity_id;
  uint8_t comp_mask;
  float rot_y;
  uint32_t tick;
} NgStateUpdate;

NgSyncMode ng_scene_sync_mode(const char *scene_id);
bool ng_scene_has_js_host(const char *scene_id);

#endif
