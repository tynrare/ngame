// agent: composer-2.5 | 2026-07-25 | viewport resize debounce | 7c1e4a
#include "ng_viewport.h"
#include <raylib.h>

static int g_width = 800;
static int g_height = 450;
static int g_requested_w = 800;
static int g_requested_h = 450;
static double g_resize_at = -1.0;
static const float NG_RESIZE_DELAY = 0.3f;

void ng_viewport_init(int width, int height) {
  g_width = width;
  g_height = height;
  g_requested_w = width;
  g_requested_h = height;
  g_resize_at = -1.0;
}

void ng_viewport_poll(void) {
  const int vw = GetScreenWidth();
  const int vh = GetScreenHeight();
  const double now = GetTime();

  if (vw != g_requested_w || vh != g_requested_h) {
    g_requested_w = vw;
    g_requested_h = vh;
    if (g_resize_at > 0.0) {
      g_resize_at = now;
      return;
    }
  }

  const bool changed = (g_requested_w != g_width || g_requested_h != g_height);
  if (changed && g_resize_at > 0.0 && (now - g_resize_at) > NG_RESIZE_DELAY) {
    g_width = g_requested_w;
    g_height = g_requested_h;
    g_resize_at = now;
  }
}

int ng_viewport_width(void) { return g_width; }

int ng_viewport_height(void) { return g_height; }
