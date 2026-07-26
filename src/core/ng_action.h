// agent: composer-2.5 | 2026-07-25 | action bounce types | f1a2b3
#ifndef NG_ACTION_H
#define NG_ACTION_H

#include "core/ng_bus.h"
#include "world/ng_world.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum NgActionKind {
  NG_ACT_NONE = 0,
  NG_ACT_SCENE_LOAD,
  NG_ACT_FULL_RESYNC,
} NgActionKind;

typedef struct NgActionResult {
  uint16_t action_seq;
  uint32_t server_tick;
  uint32_t state_hash;
  NgActionKind kind;
  char reply[256];
  NgSnapshot state;
  bool have_state;
} NgActionResult;

bool ng_action_server_exec(NgWorld *w, const NgMsg *cmd, uint16_t action_seq,
                             NgActionResult *out);
void mod_render_apply_action(const NgActionResult *result);

#endif
