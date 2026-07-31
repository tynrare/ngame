// agent: composer-2.5 | 2026-07-31 | phys save restore smoke | fa34b6
/* SyncTest-lite: Box3D Save → Restore → resim checksum must match. */
#include "box3d/box3d.h"
#include "scene/graph.h"
#include "scene/physics.h"
#include "scene/runtime.h"
#include "world/ng_world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static NgWorld g_smoke_world;

NgWorld *mod_sim_world(void) { return &g_smoke_world; }

void mod_net_flush_scene_updates(void) {}

static void smoke_fail(const char *msg) {
  fprintf(stderr, "phys_save_smoke: %s\n", msg);
  exit(1);
}

static void smoke_step(int n) {
  for (int i = 0; i < n; i++) {
    mod_scene_physics_fixed_step(1.0f / 60.0f, true, false);
  }
}

static bool smoke_setup_world(void) {
  mod_scene_runtime_use_server();
  mod_scene_graph_reset();
  mod_scene_physics_reset();
  mod_scene_physics_set_sim_mode(NG_PHYS_SIM_LOCKSTEP);
  mod_scene_physics_set_gravity(0.0f, -10.0f, 0.0f);

  if (!mod_scene_physics_describe_shape("ground_s", "box", 4.0f, 0.25f, 4.0f, 0.0f, 0.5f, false)) {
    return false;
  }
  if (!mod_scene_physics_describe_shape("box_s", "box", 0.5f, 0.5f, 0.5f, 1.0f, 0.4f, false)) {
    return false;
  }
  if (!mod_scene_physics_describe_body("ground_b", "static", "ground_s")) {
    return false;
  }
  if (!mod_scene_physics_describe_body("box_b", "dynamic", "box_s")) {
    return false;
  }
  if (!mod_scene_graph_describe("entity", "ground_e", NG_SYNC_SERVER, "", "ground_b", -1)) {
    return false;
  }
  if (!mod_scene_graph_describe("entity", "box_e", NG_SYNC_SERVER, "", "box_b", -1)) {
    return false;
  }

  const float gpos[3] = {0.0f, -0.25f, 0.0f};
  const float bpos[3] = {0.1f, 3.0f, 0.0f};
  const float rot[3] = {0.0f, 0.0f, 0.0f};
  const int gh = mod_scene_graph_spawn("ground_e", 1u, "ground_a", gpos, rot, 1.0f, NULL, -1);
  const int bh = mod_scene_graph_spawn("box_e", 2u, "box_a", bpos, rot, 1.0f, NULL, -1);
  if (gh < 0 || bh < 0) {
    return false;
  }
  if (!mod_scene_physics_attach(gh, "ground_b", NG_SYNC_SERVER, true, false, gpos, rot)) {
    return false;
  }
  if (!mod_scene_physics_attach(bh, "box_b", NG_SYNC_SERVER, true, false, bpos, rot)) {
    return false;
  }
  /* Deterministic kick so the dynamic body moves. */
  if (!mod_scene_physics_apply_impulse(bh, 0.5f, 0.0f, 0.25f)) {
    return false;
  }
  return true;
}

int main(void) {
  ng_world_init(&g_smoke_world);
  if (!smoke_setup_world()) {
    smoke_fail("setup failed");
  }

  smoke_step(20);
  const uint32_t hash0 = mod_scene_physics_checksum();
  if (hash0 == 0u) {
    smoke_fail("empty checksum after warmup");
  }

  uint8_t *blob = NULL;
  int blob_size = 0;
  if (!mod_scene_physics_export(&blob, &blob_size) || !blob || blob_size <= 0) {
    smoke_fail("export failed");
  }

  if (!mod_scene_physics_import(blob, blob_size)) {
    b3FreeSaveData(blob, blob_size);
    smoke_fail("import failed");
  }
  const uint32_t hash1 = mod_scene_physics_checksum();
  if (hash1 != hash0) {
    fprintf(stderr, "phys_save_smoke: post-import hash 0x%08x != 0x%08x\n", hash1, hash0);
    b3FreeSaveData(blob, blob_size);
    return 1;
  }

  smoke_step(20);
  const uint32_t hash2 = mod_scene_physics_checksum();

  if (!mod_scene_physics_import(blob, blob_size)) {
    b3FreeSaveData(blob, blob_size);
    smoke_fail("re-import failed");
  }
  smoke_step(20);
  const uint32_t hash3 = mod_scene_physics_checksum();
  b3FreeSaveData(blob, blob_size);

  if (hash3 != hash2) {
    fprintf(stderr, "phys_save_smoke: resim hash 0x%08x != 0x%08x\n", hash3, hash2);
    return 1;
  }

  printf("PHYS_SAVE_SMOKE ok hash0=0x%08x resim=0x%08x\n", hash0, hash2);
  return 0;
}

// agent: composer-2.5 | 2026-07-31 | phys save restore smoke | fa34b6
