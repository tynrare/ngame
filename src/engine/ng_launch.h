// agent: composer-2.5 | 2026-07-25 | launch mode parse spawn | b0c01e
// agent: composer-2.5 | 2026-07-28 | gateway agent upstream ports | 2cac03
#ifndef NG_LAUNCH_H
#define NG_LAUNCH_H

#include <stdbool.h>
#include <stdint.h>

typedef enum NgLaunchMode {
  NG_LAUNCH_REMOTE = 0,
  NG_LAUNCH_LOCAL,
  NG_LAUNCH_SOLO,
} NgLaunchMode;

typedef struct NgLaunchConfig {
  NgLaunchMode mode;
  char host[64];
  uint16_t port;
  uint16_t agent_port;
  bool use_upstream;
} NgLaunchConfig;

bool ng_launch_parse(int argc, char **argv, NgLaunchConfig *cfg);
void ng_launch_print_usage(const char *prog);
bool ng_launch_spawn_server(uint16_t port);
void ng_launch_stop_server(void);
bool ng_launch_server_spawned(void);

#endif
