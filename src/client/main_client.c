// agent: composer-2.5 | 2026-07-25 | raylib client entry | k4n62i
// agent: composer-2.5 | 2026-08-09 | client --server dispatch | 6ee7bb
#include "client/ng_app_client.h"
#include "engine/ng_launch.h"
#include "server/ng_app_server.h"
#include <raylib.h>

#if defined(PLATFORM_WEB)
#include <emscripten/emscripten.h>
#endif

int main(int argc, char **argv) {
  NgLaunchConfig launch = {0};
  if (!ng_launch_parse(argc, argv, &launch)) {
    return 1;
  }
#if !defined(PLATFORM_WEB) && !defined(__EMSCRIPTEN__)
  if (launch.mode == NG_LAUNCH_SERVER) {
    ng_app_server_init(argc, argv);
    while (ng_app_server_running()) {
      ng_app_server_frame();
    }
    ng_app_server_shutdown();
    return 0;
  }
#endif

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
// agent: composer-2.5 | 2026-07-25 | raylib client entry | k4n62i
// agent: composer-2.5 | 2026-08-09 | client --server dispatch | 6ee7bb
