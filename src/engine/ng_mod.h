// agent: composer-2.5 | 2026-07-25 | module registry lifecycle | 7d2f4b
#ifndef NG_MOD_H
#define NG_MOD_H

#include "ng_bus.h"
#include <stdbool.h>

typedef struct NgModOps {
  const char *name;
  NgBusDest dest;
  bool (*init)(void *ctx);
  void (*shutdown)(void *ctx);
  bool (*on_msg)(const NgMsg *msg, void *ctx);
} NgModOps;

bool ng_mod_register(const NgModOps *ops, void *ctx);
bool ng_mod_init_all(void);
// agent: composer-2.5 | 2026-07-29 | last init failure accessor | 4f998b
const char *ng_mod_last_init_failed(void);
void ng_mod_shutdown_all(void);
void ng_mod_publish_tick(float dt);
void ng_mod_publish_draw(void);

// agent: composer-2.5 | 2026-07-29 | last init failure accessor | 4f998b
#endif
