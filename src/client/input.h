// agent: composer-2.5 | 2026-07-25 | input sample flush module | 202daf
#ifndef MOD_INPUT_H
#define MOD_INPUT_H

#include <stdbool.h>

// agent: codex-5.3 | 2026-07-29 | add W S input bits | 84f2c9
#define NG_INPUT_A 1
#define NG_INPUT_D 2
#define NG_INPUT_W 4
#define NG_INPUT_S 8
// agent: composer-2.5 | 2026-08-01 | input KEY_F shoot bit | 364b50
#define NG_INPUT_F 16

void mod_input_begin_frame(void);
int mod_input_buttons(void);
float mod_input_take_yaw(void);
// agent: composer-2.5 | 2026-07-29 | mcp wire input mouse api | 6e1a2b
void mod_input_wire_buttons(int buttons, int frames);
void mod_input_wire_mouse(float x, float y, int frames);
void mod_input_tick_wire(void);
bool mod_input_mouse_pos(float *out_x, float *out_y);

#endif
// agent: codex-5.3 | 2026-07-29 | add W S input bits | 84f2c9
// agent: composer-2.5 | 2026-07-29 | mcp wire input mouse api | 6e1a2b
// agent: composer-2.5 | 2026-07-29 | time-based mcp wire holds | 3e9b5c
// agent: composer-2.5 | 2026-08-01 | input KEY_F shoot bit | 364b50
