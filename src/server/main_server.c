// agent: composer-2.5 | 2026-07-25 | headless server entry | l5o73j
#include "server/ng_app_server.h"

int main(int argc, char **argv) {
  ng_app_server_init(argc, argv);

  while (ng_app_server_running()) {
    ng_app_server_frame();
  }

  ng_app_server_shutdown();
  return 0;
}
