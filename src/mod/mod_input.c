// agent: composer-2.5 | 2026-07-27 | server input stub | d7e8f9
#include "mod_input.h"

#ifdef NG_SERVER

void mod_input_begin_frame(void) {}
int mod_input_buttons(void) { return 0; }
float mod_input_take_yaw(void) { return 0.0f; }

#else

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

float mod_input_take_yaw(void) {
  const float yaw = g_input.yaw_accum;
  g_input.yaw_accum = 0.0f;
  return yaw;
}

#endif
