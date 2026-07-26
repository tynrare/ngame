// agent: composer-2.5 | 2026-07-25 | sim scene interface types | d8a03f
#ifndef SIM_TYPES_H
#define SIM_TYPES_H

#include "world/ng_world.h"

typedef struct SimOps {
  const char *id;
  bool (*enter)(NgWorld *w);
  void (*exit)(NgWorld *w);
  void (*update)(NgWorld *w, float dt);
} SimOps;

const SimOps *sim_sphere_ops(void);
const SimOps *sim_cube_ops(void);
uint32_t sim_cube_entity_id(void);

#endif
