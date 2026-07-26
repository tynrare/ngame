// agent: composer-2.5 | 2026-07-25 | input sample flush module | 5d4291
#include "mod_input.h"
#include <raylib.h>

typedef struct ModInputState {
  int buttons;
  float yaw_accum;
  float pred_yaw;
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

void mod_input_apply_pred(float dt) {
  if (IsKeyDown(KEY_A)) {
    g_input.pred_yaw -= 1.5f * dt;
  }
  if (IsKeyDown(KEY_D)) {
    g_input.pred_yaw += 1.5f * dt;
  }
}

int mod_input_buttons(void) { return g_input.buttons; }

float mod_input_take_yaw(void) {
  const float yaw = g_input.yaw_accum;
  g_input.yaw_accum = 0.0f;
  return yaw;
}

float mod_input_pred_yaw(void) { return g_input.pred_yaw; }

void mod_input_set_pred_yaw(float yaw) { g_input.pred_yaw = yaw; }
