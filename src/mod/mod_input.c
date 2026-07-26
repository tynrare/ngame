// agent: composer-2.5 | 2026-07-25 | input sample flush module | 5d4291
#include "mod_input.h"
#include <raylib.h>

typedef struct ModInputState {
  int buttons;
  float yaw_accum;
} ModInputState;

static ModInputState g_input;

void mod_input_begin_frame(void) {
  int buttons = 0;
  if (IsKeyDown(KEY_A)) {
    buttons |= NG_INPUT_A;
  }
  if (IsKeyDown(KEY_D)) {
    buttons |= NG_INPUT_D;
  }
  g_input.buttons = buttons;

  if (IsKeyDown(KEY_LEFT)) {
    g_input.yaw_accum -= 0.05f;
  }
  if (IsKeyDown(KEY_RIGHT)) {
    g_input.yaw_accum += 0.05f;
  }
}

int mod_input_buttons(void) { return g_input.buttons; }

// agent: composer-2.5 | 2026-07-26 | drop pred yaw input helpers | 6e07e9
float mod_input_take_yaw(void) {
  const float yaw = g_input.yaw_accum;
  g_input.yaw_accum = 0.0f;
  return yaw;
}
