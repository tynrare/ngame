// agent: composer-2.5 | 2026-07-25 | headless server entry | l5o73j
// agent: composer-2.5 | 2026-07-25 | server 60hz frame sleep | fc789c
#include "server/ng_app_server.h"
#include <time.h>
#include <unistd.h>

#define NG_SERVER_FRAME_US 16000

int main(int argc, char **argv) {
  ng_app_server_init(argc, argv);

  while (ng_app_server_running()) {
    ng_app_server_frame();
    usleep(NG_SERVER_FRAME_US);
  }

  ng_app_server_shutdown();
  return 0;
}
