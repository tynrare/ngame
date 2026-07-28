// agent: composer-2.5 | 2026-07-25 | SoA entity world store | c4d91e
#ifndef NG_WORLD_H
#define NG_WORLD_H

#include <stdbool.h>
#include <stdint.h>

// agent: composer-2.5 | 2026-07-25 | web smaller snapshot caps | d34d5e
#if defined(__EMSCRIPTEN__)
#define NG_WORLD_ENTITY_MAX 256
#define NG_SNAPSHOT_ENTITY_MAX 64
#else
#define NG_WORLD_ENTITY_MAX 4096
#define NG_SNAPSHOT_ENTITY_MAX NG_WORLD_ENTITY_MAX
#endif
#define NG_WORLD_GRID_DIM   64
#define NG_WORLD_GRID_CELL  16.0f

typedef enum NgEntityType {
  NG_ENTITY_NONE = 0,
  NG_ENTITY_SPHERE = 1,
  NG_ENTITY_CUBE = 2,
} NgEntityType;

typedef enum NgCompMask {
  NG_COMP_TYPE  = 1u << 0,
  NG_COMP_POS   = 1u << 1,
  NG_COMP_ROT   = 1u << 2,
  NG_COMP_PHASE = 1u << 3,
  NG_COMP_FLAGS = 1u << 4,
  NG_COMP_SCALE = 1u << 5,
  NG_COMP_ALL   = 0x3fu,
} NgCompMask;

typedef struct NgEntitySnap {
  uint32_t id;
  uint8_t type;
  float pos[3];
  float rot_y;
  float phase;
  uint32_t flags;
  uint32_t comp_mask;
} NgEntitySnap;

typedef struct NgSnapshot {
  uint32_t tick;
  char scene_id[32];
  int entity_count;
  NgEntitySnap entities[NG_SNAPSHOT_ENTITY_MAX];
} NgSnapshot;

typedef struct NgWorld {
  uint32_t gen[NG_WORLD_ENTITY_MAX];
  uint8_t alive[NG_WORLD_ENTITY_MAX];
  uint8_t type[NG_WORLD_ENTITY_MAX];
  float pos_x[NG_WORLD_ENTITY_MAX];
  float pos_y[NG_WORLD_ENTITY_MAX];
  float pos_z[NG_WORLD_ENTITY_MAX];
  float rot_y[NG_WORLD_ENTITY_MAX];
  float phase[NG_WORLD_ENTITY_MAX];
  uint32_t flags[NG_WORLD_ENTITY_MAX];
  uint32_t public_id[NG_WORLD_ENTITY_MAX];
  int grid_cell[NG_WORLD_ENTITY_MAX];
  int grid_next[NG_WORLD_ENTITY_MAX];
  int grid_head[NG_WORLD_GRID_DIM * NG_WORLD_GRID_DIM];
  int live_count;
  uint32_t tick;
  char scene_id[32];
} NgWorld;

void ng_world_init(NgWorld *w);
void ng_world_clear(NgWorld *w);
uint32_t ng_world_spawn(NgWorld *w, NgEntityType type, float x, float y, float z);
void ng_world_despawn(NgWorld *w, uint32_t id);
int ng_world_find_index(NgWorld *w, uint32_t id);
void ng_world_rebuild_grid(NgWorld *w);
void ng_world_grid_insert(NgWorld *w, int idx);
void ng_world_set_scene(NgWorld *w, const char *scene_id);
void ng_world_set_entity_state(NgWorld *w, uint32_t id, float x, float y, float z, float rot_y,
                               float phase);
void ng_world_set_public_id(NgWorld *w, uint32_t packed_id, uint32_t public_id);
void ng_world_fill_snapshot(NgWorld *w, NgSnapshot *out);
void ng_world_fill_snapshot_aoi(NgWorld *w, NgSnapshot *out, float cx, float cy,
                                float radius, uint32_t tick_mod);
void ng_world_fill_snapshot_delta(NgWorld *w, NgSnapshot *cur, const NgSnapshot *baseline,
                                  NgSnapshot *out);

// agent: composer-2.5 | 2026-07-25 | FNV-1a world state hash | c4e8d1
uint32_t ng_world_hash(const NgWorld *w);

#endif
