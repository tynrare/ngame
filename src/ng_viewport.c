// agent: composer-2.5 | 2026-07-25 | instant viewport sync | e2b4c6
#include "ng_viewport.h"
#include <raylib.h>

static int g_width = 800;
static int g_height = 450;

void ng_viewport_init(int width, int height) {
  g_width = width > 0 ? width : 800;
  g_height = height > 0 ? height : 450;
}

void ng_viewport_poll(void) {
  g_width = GetScreenWidth();
  g_height = GetScreenHeight();
}

int ng_viewport_width(void) { return g_width; }

int ng_viewport_height(void) { return g_height; }
