// agent: composer-2.5 | 2026-07-25 | module registry lifecycle | 7d2f4b
// agent: composer-2.5 | 2026-07-29 | Extend NgModOps side fixed_step | 8fecb5
#ifndef NG_MOD_H
#define NG_MOD_H

#include "ng_bus.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum NgModSide {
  NG_MOD_SIDE_SERVER = 1,
  NG_MOD_SIDE_CLIENT = 2,
  NG_MOD_SIDE_BOTH = 3,
} NgModSide;

typedef struct NgModOps {
  const char *name;
  NgBusDest dest;
  NgModSide side;
  bool (*init)(void *ctx);
  void (*shutdown)(void *ctx);
  bool (*on_msg)(const NgMsg *msg, void *ctx);
  void (*fixed_step)(void *ctx, float fixed_dt, uint32_t tick);
} NgModOps;

#define NG_MOD_FIXED_DT (1.0f / 60.0f)
#define NG_MOD_FIXED_MAX_STEPS 4

typedef enum NgFixedGate {
  NG_FIXED_GATE_GO = 0,
  NG_FIXED_GATE_BUFFER = 1,
  NG_FIXED_GATE_STALL = 2,
} NgFixedGate;

typedef NgFixedGate (*NgFixedGateFn)(void);

bool ng_mod_register(const NgModOps *ops, void *ctx);
bool ng_mod_init_all(void);
// agent: composer-2.5 | 2026-07-29 | last init failure accessor | 4f998b
const char *ng_mod_last_init_failed(void);
void ng_mod_shutdown_all(void);
void ng_mod_set_fixed_gate(NgFixedGateFn fn);
void ng_mod_publish_tick(float dt);
void ng_mod_publish_draw(void);

// agent: composer-2.5 | 2026-07-29 | last init failure accessor | 4f998b
// agent: composer-2.5 | 2026-07-29 | Extend NgModOps side fixed_step | 8fecb5
// agent: composer-2.5 | 2026-07-29 | lockstep fixed tick gate | d01c44
#endif
