// agent: composer-2.5 | 2026-07-25 | input sample flush module | 202daf
#ifndef MOD_INPUT_H
#define MOD_INPUT_H

#include <stdbool.h>

#define NG_INPUT_A 1
#define NG_INPUT_D 2

void mod_input_begin_frame(void);
void mod_input_apply_pred(float dt);
int mod_input_buttons(void);
float mod_input_take_yaw(void);
float mod_input_pred_yaw(void);
void mod_input_set_pred_yaw(float yaw);

#endif
