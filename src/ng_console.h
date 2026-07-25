// agent: composer-2.5 | 2026-07-25 | tab overlay console | 6b2e9a
#ifndef NG_CONSOLE_H
#define NG_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>

#define NG_CONSOLE_INPUT_MAX 256
#define NG_CONSOLE_OUTPUT_MAX 1024
#define NG_CONSOLE_LINES 15
#define NG_CONSOLE_LINE_H 20

typedef struct NgConsole {
  char input[NG_CONSOLE_INPUT_MAX];
  char last_cmd[NG_CONSOLE_INPUT_MAX];
  char output[NG_CONSOLE_OUTPUT_MAX];
  int input_len;
  bool open;
  bool submitted;
} NgConsole;

void ng_console_init(NgConsole *c);
void ng_console_update(NgConsole *c);
void ng_console_draw(const NgConsole *c);
bool ng_console_blocks_scene(const NgConsole *c);
const char *ng_console_take_line(NgConsole *c);
void ng_console_set_output(NgConsole *c, const char *text);

#endif
