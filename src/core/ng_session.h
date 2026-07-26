// agent: composer-2.5 | 2026-07-26 | session and state update types | a1b2c3
#ifndef NG_SESSION_H
#define NG_SESSION_H

#include <stdbool.h>
#include <stdint.h>

typedef struct NgSessionState {
  char scene_id[32];
  uint32_t tick;
  uint8_t controller_id;
  uint8_t your_id;
  uint32_t cube_entity_id;
  bool client_fields;
} NgSessionState;

typedef struct NgStateUpdate {
  uint32_t entity_id;
  float rot_y;
  uint32_t tick;
} NgStateUpdate;

bool ng_scene_client_fields(const char *scene_id);

#endif
