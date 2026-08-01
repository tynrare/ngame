// agent: composer-2.5 | 2026-07-27 | server input stub | d7e8f9
#include "input.h"

#ifdef NG_SERVER

void mod_input_begin_frame(void) {}
int mod_input_buttons(void) { return 0; }
float mod_input_take_yaw(void) { return 0.0f; }
void mod_input_wire_buttons(int buttons, int frames) {
  (void)buttons;
  (void)frames;
}
void mod_input_wire_mouse(float x, float y, int frames) {
  (void)x;
  (void)y;
  (void)frames;
}
void mod_input_tick_wire(void) {}
bool mod_input_mouse_pos(float *out_x, float *out_y) {
  if (out_x) {
    *out_x = 0.0f;
  }
  if (out_y) {
    *out_y = 0.0f;
  }
  return false;
}

#else

#include <raylib.h>

typedef struct ModInputState {
  int buttons;
  float yaw_accum;
  // agent: composer-2.5 | 2026-07-29 | time-based mcp wire holds | 3e9b5c
  int wired_buttons;
  double wired_buttons_until;
  bool wired_mouse;
  float wired_mouse_x;
  float wired_mouse_y;
  double wired_mouse_until;
  // agent: composer-2.5 | 2026-07-29 | sticky mouse after wire | 28dc7b
  bool wired_mouse_sticky;
} ModInputState;

static ModInputState g_input;

static double mod_input_wire_deadline(int frames) {
  const double secs = (frames > 0 ? (double)frames : 30.0) / 60.0;
  return GetTime() + secs;
}

// agent: composer-2.5 | 2026-07-30 | input buttons include live wire | 2a23b1
static int mod_input_sample_buttons(void) {
  int buttons = 0;
  // agent: codex-5.3 | 2026-07-29 | map W S keys | 26672c
  if (IsKeyDown(KEY_A)) {
    buttons |= NG_INPUT_A;
  }
  if (IsKeyDown(KEY_D)) {
    buttons |= NG_INPUT_D;
  }
  if (IsKeyDown(KEY_W)) {
    buttons |= NG_INPUT_W;
  }
  if (IsKeyDown(KEY_S)) {
    buttons |= NG_INPUT_S;
  }
  // agent: composer-2.5 | 2026-08-01 | map KEY_F live input | ec1f2a
  if (IsKeyDown(KEY_F)) {
    buttons |= NG_INPUT_F;
  }
  /* Wire must apply even if begin_frame has not run yet this frame. */
  if (g_input.wired_buttons_until > 0.0 && GetTime() < g_input.wired_buttons_until) {
    buttons |= g_input.wired_buttons;
  }
  return buttons;
}

void mod_input_begin_frame(void) {
  g_input.buttons = mod_input_sample_buttons();

  if (IsKeyDown(KEY_LEFT)) {
    g_input.yaw_accum -= 0.05f;
  }
  if (IsKeyDown(KEY_RIGHT)) {
    g_input.yaw_accum += 0.05f;
  }
}

int mod_input_buttons(void) {
  /* Always re-sample so fixed_step / get_input see wire + keys live. */
  g_input.buttons = mod_input_sample_buttons();
  return g_input.buttons;
}

float mod_input_take_yaw(void) {
  const float yaw = g_input.yaw_accum;
  g_input.yaw_accum = 0.0f;
  return yaw;
}

void mod_input_wire_buttons(int buttons, int frames) {
  g_input.wired_buttons = buttons;
  g_input.wired_buttons_until = mod_input_wire_deadline(frames);
}

void mod_input_wire_mouse(float x, float y, int frames) {
  g_input.wired_mouse = true;
  g_input.wired_mouse_sticky = false;
  g_input.wired_mouse_x = x;
  g_input.wired_mouse_y = y;
  g_input.wired_mouse_until = mod_input_wire_deadline(frames);
}

void mod_input_tick_wire(void) {
  const double now = GetTime();
  if (g_input.wired_buttons_until > 0.0 && now >= g_input.wired_buttons_until) {
    g_input.wired_buttons = 0;
    g_input.wired_buttons_until = 0.0;
  }
  // agent: composer-2.5 | 2026-07-29 | sticky mouse after wire | 28dc7b
  // After hold ends, keep wired coords latched (no OS snap) until next wire_mouse.
  if (g_input.wired_mouse && !g_input.wired_mouse_sticky &&
      g_input.wired_mouse_until > 0.0 && now >= g_input.wired_mouse_until) {
    g_input.wired_mouse_sticky = true;
    g_input.wired_mouse_until = 0.0;
  }
}

bool mod_input_mouse_pos(float *out_x, float *out_y) {
  // agent: composer-2.5 | 2026-07-29 | sticky mouse after wire | 28dc7b
  if (g_input.wired_mouse) {
    if (out_x) {
      *out_x = g_input.wired_mouse_x;
    }
    if (out_y) {
      *out_y = g_input.wired_mouse_y;
    }
    return true;
  }
  const Vector2 p = GetMousePosition();
  if (out_x) {
    *out_x = p.x;
  }
  if (out_y) {
    *out_y = p.y;
  }
  return true;
}

#endif
// agent: codex-5.3 | 2026-07-29 | map W S keys | 26672c
// agent: composer-2.5 | 2026-07-29 | mcp wire input mouse state | a91c4d
// agent: composer-2.5 | 2026-07-29 | time-based mcp wire holds | 3e9b5c
// agent: composer-2.5 | 2026-07-29 | sticky mouse after wire | 28dc7b
// agent: composer-2.5 | 2026-07-30 | input buttons include live wire | 2a23b1
// agent: composer-2.5 | 2026-08-01 | map KEY_F live input | ec1f2a
