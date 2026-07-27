// agent: composer-2.5 | 2026-07-27 | session spawn table only | a8b9c0
#ifndef NG_SESSION_H
#define NG_SESSION_H

#include "ng_sync.h"
#include <stdbool.h>
#include <stdint.h>

#define NG_SESSION_SPAWN_MAX 8

typedef struct NgSessionSpawn {
  uint32_t entity_id;
  char desc_name[32];
  NgSyncMode sync;
} NgSessionSpawn;

typedef struct NgSessionState {
  char scene_id[32];
  uint32_t tick;
  uint8_t controller_id;
  uint8_t your_id;
  NgSyncMode scene_sync;
  int spawn_count;
  NgSessionSpawn spawns[NG_SESSION_SPAWN_MAX];
} NgSessionState;

typedef struct NgStateUpdate {
  uint32_t entity_id;
  uint16_t seq;
  uint8_t comp_mask;
  uint32_t tick;
  float pos[3];
  float rot[3];
  float scale;
} NgStateUpdate;

#endif
