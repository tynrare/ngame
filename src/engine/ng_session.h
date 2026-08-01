// agent: composer-2.5 | 2026-07-27 | session spawn table only | a8b9c0
// agent: composer-2.5 | 2026-07-29 | session spawn key transform | 9339a6
// agent: composer-2.5 | 2026-07-29 | lockstep session sim flag | 8766dd
// agent: composer-2.5 | 2026-07-30 | session syncing snap tick | 75da2a
#ifndef NG_SESSION_H
#define NG_SESSION_H

#include "ng_sync.h"
#include <stdbool.h>
#include <stdint.h>

#define NG_SESSION_SPAWN_MAX 32

typedef struct NgSessionSpawn {
  uint32_t entity_id;
  char desc_name[32];
  char key[32];
  NgSyncMode sync;
  float pos[3];
  float rot[3];
  float scale;
} NgSessionSpawn;

typedef struct NgSessionState {
  char scene_id[32];
  uint32_t tick;
  uint8_t controller_id;
  uint8_t your_id;
  NgSyncMode scene_sync;
  /* 0=off, 1=pure lockstep, 2=hybrid (proto v11). Non-zero = input-sim family. */
  // agent: composer-2.5 | 2026-08-01 | session sim mode comment | 4a9b0e
  uint8_t lockstep;
  uint8_t syncing;
  uint32_t snap_tick;
  /* Host-assigned local send-ahead (0 = client keeps current / default). */
  // agent: composer-2.5 | 2026-08-01 | session playout field | 50d23e
  uint8_t playout;
  int spawn_count;
  NgSessionSpawn spawns[NG_SESSION_SPAWN_MAX];
} NgSessionState;

// agent: composer-2.5 | 2026-07-30 | state update lin ang vel | 16a0ac
typedef struct NgStateUpdate {
  uint32_t entity_id;
  uint16_t seq;
  uint8_t comp_mask;
  uint32_t tick;
  float pos[3];
  float rot[3];
  float scale;
  float lin_vel[3];
  float ang_vel[3];
} NgStateUpdate;

#endif
// agent: composer-2.5 | 2026-07-29 | session spawn key transform | 9339a6
// agent: composer-2.5 | 2026-07-29 | lockstep session sim flag | 8766dd
// agent: composer-2.5 | 2026-07-30 | session syncing snap tick | 75da2a
// agent: composer-2.5 | 2026-07-30 | state update lin ang vel | 16a0ac
// agent: composer-2.5 | 2026-08-01 | session playout field | 50d23e
// agent: composer-2.5 | 2026-08-01 | session sim mode comment | 4a9b0e
