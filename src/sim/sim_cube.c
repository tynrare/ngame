// agent: composer-2.5 | 2026-07-25 | cube sim no raylib | f2c15b
#include "sim_types.h"

#define NG_INPUT_A 1
#define NG_INPUT_D 2

static uint32_t g_cube_entity;

static bool sim_cube_enter(NgWorld *w) {
  g_cube_entity = ng_world_spawn(w, NG_ENTITY_CUBE, 0.0f, 0.0f, 0.0f);
  return g_cube_entity != 0;
}

static void sim_cube_exit(NgWorld *w) {
  if (g_cube_entity) {
    ng_world_despawn(w, g_cube_entity);
    g_cube_entity = 0;
  }
}

// agent: composer-2.5 | 2026-07-26 | server cube rot client owned | c3d4e5
static void sim_cube_update(NgWorld *w, float dt) {
  const int idx = ng_world_find_index(w, g_cube_entity);
  if (idx < 0) {
    return;
  }
  w->phase[idx] += dt;
}

uint32_t sim_cube_entity_id(void) { return g_cube_entity; }

static const SimOps g_sim_cube = {
    .id = "cube",
    .enter = sim_cube_enter,
    .exit = sim_cube_exit,
    .update = sim_cube_update,
};

const SimOps *sim_cube_ops(void) { return &g_sim_cube; }
