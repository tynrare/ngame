// agent: composer-2.5 | 2026-07-25 | entry point native web | 0a5e7d
#include "ng_app.h"
#include <raylib.h>

#if defined(PLATFORM_WEB)
#include <emscripten/emscripten.h>
#endif

int main(void) {
  const int width = 800;
  const int height = 450;

  InitWindow(width, height, "ngame");
  SetWindowState(FLAG_WINDOW_RESIZABLE);
  SetTargetFPS(60);

  ng_app_init();

#if defined(PLATFORM_WEB)
  emscripten_set_main_loop(ng_app_frame, 0, 1);
#else
  while (!WindowShouldClose()) {
    ng_app_frame();
  }
#endif

  ng_app_shutdown();
  CloseWindow();
  return 0;
}
