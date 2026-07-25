// agent: composer-2.5 | 2026-07-25 | tab overlay console | 6b2e9a
#include "ng_console.h"
#include <raylib.h>
#include <string.h>

void ng_console_init(NgConsole *c) {
  if (!c) {
    return;
  }
  memset(c, 0, sizeof(*c));
  c->open = false;
  strncpy(c->output,
          "Tab: toggle console\nscene sphere | scene cube",
          NG_CONSOLE_OUTPUT_MAX - 1);
}

void ng_console_update(NgConsole *c) {
  if (!c) {
    return;
  }

  if (IsKeyPressed(KEY_TAB)) {
    c->open = !c->open;
    c->submitted = false;
    return;
  }

  if (!c->open) {
    return;
  }

  int key = GetCharPressed();
  while (key > 0) {
    if (key >= 32 && key <= 125 && c->input_len < NG_CONSOLE_INPUT_MAX - 1) {
      c->input[c->input_len++] = (char)key;
      c->input[c->input_len] = '\0';
    }
    key = GetCharPressed();
  }

  if (IsKeyPressed(KEY_BACKSPACE) && c->input_len > 0) {
    c->input_len--;
    c->input[c->input_len] = '\0';
  }

  if (IsKeyPressed(KEY_ENTER)) {
    c->submitted = true;
  }
}

bool ng_console_blocks_scene(const NgConsole *c) {
  return c && c->open;
}

const char *ng_console_take_line(NgConsole *c) {
  if (!c || !c->submitted) {
    return NULL;
  }

  c->submitted = false;
  if (c->input_len == 0) {
    return NULL;
  }

  strncpy(c->last_cmd, c->input, NG_CONSOLE_INPUT_MAX - 1);
  c->last_cmd[NG_CONSOLE_INPUT_MAX - 1] = '\0';

  c->input[0] = '\0';
  c->input_len = 0;

  return c->last_cmd;
}

void ng_console_set_output(NgConsole *c, const char *text) {
  if (!c || !text) {
    return;
  }
  strncpy(c->output, text, NG_CONSOLE_OUTPUT_MAX - 1);
  c->output[NG_CONSOLE_OUTPUT_MAX - 1] = '\0';
}

void ng_console_draw(const NgConsole *c) {
  if (!c || !c->open) {
    return;
  }

  const int w = GetScreenWidth() - 2;
  const int panel_h = NG_CONSOLE_LINE_H * NG_CONSOLE_LINES;

  DrawRectangle(1, 1, w, panel_h, (Color){0, 0, 0, 100});
  DrawText(TextFormat("> %s", c->last_cmd), 5, 2, NG_CONSOLE_LINE_H, WHITE);
  DrawText(c->output, 10, NG_CONSOLE_LINE_H + 2, NG_CONSOLE_LINE_H, WHITE);

  const int row_y = 1 + NG_CONSOLE_LINE_H * (NG_CONSOLE_LINES - 1);
  DrawRectangle(1, row_y, w, NG_CONSOLE_LINE_H, Fade(BLACK, 0.5f));
  DrawRectangleLines(1, row_y, w, NG_CONSOLE_LINE_H, GREEN);
  DrawText(TextFormat("< %s", c->input), 5, row_y + 2, NG_CONSOLE_LINE_H, WHITE);
}
