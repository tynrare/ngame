// agent: composer-2.5 | 2026-07-25 | raylib client entry | k4n62i
#include "client/ng_app_client.h"
#include <raylib.h>

#if defined(PLATFORM_WEB)
#include <emscripten/emscripten.h>
#endif

int main(int argc, char **argv) {
  const int width = 800;
  const int height = 450;

  InitWindow(width, height, "ngame");
  SetWindowState(FLAG_WINDOW_RESIZABLE);
  SetTargetFPS(60);

  ng_app_client_init(argc, argv);

#if defined(PLATFORM_WEB)
  emscripten_set_main_loop(ng_app_client_frame, 0, 1);
#else
  while (!WindowShouldClose()) {
    ng_app_client_frame();
  }
#endif

  ng_app_client_shutdown();
  CloseWindow();
  return 0;
}
