// agent: composer-2.5 | 2026-07-25 | sphere sim no raylib | e1b04a
#include "sim_types.h"

static uint32_t g_sphere_entity;

static bool sim_sphere_enter(NgWorld *w) {
  g_sphere_entity = ng_world_spawn(w, NG_ENTITY_SPHERE, 0.0f, 0.0f, 0.0f);
  return g_sphere_entity != 0;
}

static void sim_sphere_exit(NgWorld *w) {
  if (g_sphere_entity) {
    ng_world_despawn(w, g_sphere_entity);
    g_sphere_entity = 0;
  }
}

static void sim_sphere_update(NgWorld *w, float dt) {
  const int idx = ng_world_find_index(w, g_sphere_entity);
  if (idx < 0) {
    return;
  }
  w->phase[idx] += dt;
}

static const SimOps g_sim_sphere = {
    .id = "sphere",
    .enter = sim_sphere_enter,
    .exit = sim_sphere_exit,
    .update = sim_sphere_update,
};

const SimOps *sim_sphere_ops(void) { return &g_sim_sphere; }
