// agent: composer-2.5 | 2026-07-25 | SoA entity world store | c4d91e
#include "ng_world.h"
#include <math.h>
#include <string.h>

static int ng_world_grid_xy(float x, float z) {
  int gx = (int)floorf(x / NG_WORLD_GRID_CELL);
  int gy = (int)floorf(z / NG_WORLD_GRID_CELL);
  if (gx < 0) {
    gx = 0;
  }
  if (gy < 0) {
    gy = 0;
  }
  if (gx >= NG_WORLD_GRID_DIM) {
    gx = NG_WORLD_GRID_DIM - 1;
  }
  if (gy >= NG_WORLD_GRID_DIM) {
    gy = NG_WORLD_GRID_DIM - 1;
  }
  return gy * NG_WORLD_GRID_DIM + gx;
}

static uint32_t ng_world_pack_id(const NgWorld *w, int idx) {
  return ((uint32_t)(idx + 1) << 16) | (w->gen[idx] & 0xffffu);
}

static int ng_world_unpack_index(uint32_t id) {
  return (int)((id >> 16) - 1);
}

void ng_world_init(NgWorld *w) {
  memset(w, 0, sizeof(*w));
  for (int i = 0; i < NG_WORLD_ENTITY_MAX; i++) {
    w->gen[i] = 1;
  }
  strncpy(w->scene_id, "none", sizeof(w->scene_id) - 1);
}

void ng_world_clear(NgWorld *w) {
  memset(w->alive, 0, sizeof(w->alive));
  memset(w->grid_head, -1, sizeof(w->grid_head));
  memset(w->grid_next, -1, sizeof(w->grid_next));
  memset(w->public_id, 0, sizeof(w->public_id));
  w->live_count = 0;
}

void ng_world_set_scene(NgWorld *w, const char *scene_id) {
  if (!w || !scene_id) {
    return;
  }
  ng_world_clear(w);
  strncpy(w->scene_id, scene_id, sizeof(w->scene_id) - 1);
  w->scene_id[sizeof(w->scene_id) - 1] = '\0';
}

uint32_t ng_world_spawn(NgWorld *w, NgEntityType type, float x, float y, float z) {
  for (int i = 0; i < NG_WORLD_ENTITY_MAX; i++) {
    if (w->alive[i]) {
      continue;
    }
    w->alive[i] = 1;
    w->type[i] = (uint8_t)type;
    w->pos_x[i] = x;
    w->pos_y[i] = y;
    w->pos_z[i] = z;
    w->rot_y[i] = 0.0f;
    w->phase[i] = 0.0f;
    w->flags[i] = 0;
    w->live_count++;
    ng_world_grid_insert(w, i);
    return ng_world_pack_id(w, i);
  }
  return 0;
}

void ng_world_despawn(NgWorld *w, uint32_t id) {
  const int idx = ng_world_unpack_index(id);
  if (idx < 0 || idx >= NG_WORLD_ENTITY_MAX || !w->alive[idx]) {
    return;
  }
  if ((ng_world_pack_id(w, idx) & 0xffffffffu) != id) {
    return;
  }
  w->alive[idx] = 0;
  w->gen[idx]++;
  w->live_count--;
  ng_world_rebuild_grid(w);
}

int ng_world_find_index(NgWorld *w, uint32_t id) {
  const int idx = ng_world_unpack_index(id);
  if (idx < 0 || idx >= NG_WORLD_ENTITY_MAX || !w->alive[idx]) {
    return -1;
  }
  if (ng_world_pack_id(w, idx) != id) {
    return -1;
  }
  return idx;
}

void ng_world_set_public_id(NgWorld *w, uint32_t packed_id, uint32_t public_id) {
  const int idx = ng_world_find_index(w, packed_id);
  if (idx < 0) {
    return;
  }
  w->public_id[idx] = public_id;
}

void ng_world_set_entity_state(NgWorld *w, uint32_t id, float x, float y, float z, float rot_y,
                               float phase) {
  const int idx = ng_world_find_index(w, id);
  if (idx < 0) {
    return;
  }
  w->pos_x[idx] = x;
  w->pos_y[idx] = y;
  w->pos_z[idx] = z;
  w->rot_y[idx] = rot_y;
  w->phase[idx] = phase;
  ng_world_rebuild_grid(w);
}

void ng_world_rebuild_grid(NgWorld *w) {
  memset(w->grid_head, -1, sizeof(w->grid_head));
  memset(w->grid_next, -1, sizeof(w->grid_next));
  for (int i = 0; i < NG_WORLD_ENTITY_MAX; i++) {
    if (w->alive[i]) {
      ng_world_grid_insert(w, i);
    }
  }
}

void ng_world_grid_insert(NgWorld *w, int idx) {
  const int cell = ng_world_grid_xy(w->pos_x[idx], w->pos_z[idx]);
  w->grid_cell[idx] = cell;
  w->grid_next[idx] = w->grid_head[cell];
  w->grid_head[cell] = idx;
}

static void ng_world_snap_entity(NgWorld *w, int idx, NgEntitySnap *e) {
  e->id = w->public_id[idx] ? w->public_id[idx] : ng_world_pack_id(w, idx);
  e->type = w->type[idx];
  e->pos[0] = w->pos_x[idx];
  e->pos[1] = w->pos_y[idx];
  e->pos[2] = w->pos_z[idx];
  e->rot_y = w->rot_y[idx];
  e->phase = w->phase[idx];
  e->flags = w->flags[idx];
  e->comp_mask = NG_COMP_ALL;
}

static bool ng_world_tier_visible(int dist_cells, uint32_t tick) {
  if (dist_cells <= 1) {
    return true;
  }
  if (dist_cells <= 3) {
    return (tick % 2u) == 0u;
  }
  if (dist_cells <= 6) {
    return (tick % 4u) == 0u;
  }
  return (tick % 8u) == 0u;
}

// agent: composer-2.5 | 2026-07-26 | fix fill snapshot hot path | 0ddb84
void ng_world_fill_snapshot(NgWorld *w, NgSnapshot *out) {
  if (!w || !out) {
    return;
  }
  out->tick = w->tick;
  strncpy(out->scene_id, w->scene_id, sizeof(out->scene_id) - 1);
  out->scene_id[sizeof(out->scene_id) - 1] = '\0';
  out->entity_count = 0;
  for (int i = 0; i < NG_WORLD_ENTITY_MAX; i++) {
    if (!w->alive[i]) {
      continue;
    }
    if (out->entity_count >= NG_SNAPSHOT_ENTITY_MAX) {
      break;
    }
    ng_world_snap_entity(w, i, &out->entities[out->entity_count++]);
  }
}

void ng_world_fill_snapshot_aoi(NgWorld *w, NgSnapshot *out, float cx, float cy,
                                float radius, uint32_t tick_mod) {
  if (!w || !out) {
    return;
  }
  out->tick = w->tick;
  strncpy(out->scene_id, w->scene_id, sizeof(out->scene_id) - 1);
  out->scene_id[sizeof(out->scene_id) - 1] = '\0';
  out->entity_count = 0;

  const int center = ng_world_grid_xy(cx, cy);
  const int cgx = center % NG_WORLD_GRID_DIM;
  const int cgy = center / NG_WORLD_GRID_DIM;
  const int cell_radius = (int)ceilf(radius / NG_WORLD_GRID_CELL);

  for (int dy = -cell_radius; dy <= cell_radius; dy++) {
    for (int dx = -cell_radius; dx <= cell_radius; dx++) {
      const int gx = cgx + dx;
      const int gy = cgy + dy;
      if (gx < 0 || gy < 0 || gx >= NG_WORLD_GRID_DIM || gy >= NG_WORLD_GRID_DIM) {
        continue;
      }
      const int dist = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx)
                                                                  : (dy < 0 ? -dy : dy);
      if (!ng_world_tier_visible(dist, tick_mod)) {
        continue;
      }
      const int cell = gy * NG_WORLD_GRID_DIM + gx;
      for (int idx = w->grid_head[cell]; idx >= 0; idx = w->grid_next[idx]) {
        if (out->entity_count >= NG_SNAPSHOT_ENTITY_MAX) {
          return;
        }
        const float ex = w->pos_x[idx] - cx;
        const float ez = w->pos_z[idx] - cy;
        if (ex * ex + ez * ez > radius * radius) {
          continue;
        }
        ng_world_snap_entity(w, idx, &out->entities[out->entity_count++]);
      }
    }
  }
}

static float ng_quant_cm(float v) { return roundf(v * 100.0f) / 100.0f; }
static float ng_quant_deg(float v) { return roundf(v * 57.2958f) / 57.2958f; }

void ng_world_fill_snapshot_delta(NgWorld *w, NgSnapshot *cur, const NgSnapshot *baseline,
                                  NgSnapshot *out) {
  if (!w || !cur || !baseline || !out) {
    return;
  }
  *out = *cur;
  out->entity_count = 0;

  for (int i = 0; i < cur->entity_count; i++) {
    const NgEntitySnap *c = &cur->entities[i];
    const NgEntitySnap *b = NULL;
    for (int j = 0; j < baseline->entity_count; j++) {
      if (baseline->entities[j].id == c->id) {
        b = &baseline->entities[j];
        break;
      }
    }

    NgEntitySnap *e = &out->entities[out->entity_count];
    *e = *c;
    e->comp_mask = 0;

    if (!b) {
      e->comp_mask = NG_COMP_ALL;
      out->entity_count++;
      continue;
    }

    if (b->type != c->type) {
      e->comp_mask |= NG_COMP_TYPE;
    }
    if (ng_quant_cm(b->pos[0]) != ng_quant_cm(c->pos[0]) ||
        ng_quant_cm(b->pos[1]) != ng_quant_cm(c->pos[1]) ||
        ng_quant_cm(b->pos[2]) != ng_quant_cm(c->pos[2])) {
      e->comp_mask |= NG_COMP_POS;
    }
    if (ng_quant_deg(b->rot_y) != ng_quant_deg(c->rot_y)) {
      e->comp_mask |= NG_COMP_ROT;
    }
    if (fabsf(b->phase - c->phase) > 0.001f) {
      e->comp_mask |= NG_COMP_PHASE;
    }
    if (b->flags != c->flags) {
      e->comp_mask |= NG_COMP_FLAGS;
    }

    if (e->comp_mask != 0) {
      out->entity_count++;
    }
  }
}

// agent: composer-2.5 | 2026-07-25 | FNV-1a world state hash | c4e8d1
static uint32_t ng_hash_u32(uint32_t h, uint32_t v) {
  h ^= v;
  return h * 16777619u;
}

static uint32_t ng_hash_f32(uint32_t h, float v) {
  uint32_t u;
  memcpy(&u, &v, sizeof(u));
  return ng_hash_u32(h, u);
}

uint32_t ng_world_hash(const NgWorld *w) {
  if (!w) {
    return 0;
  }
  uint32_t h = 2166136261u;
  h = ng_hash_u32(h, w->tick);
  for (int i = 0; w->scene_id[i] != '\0'; i++) {
    h = ng_hash_u32(h, (uint8_t)w->scene_id[i]);
  }
  h = ng_hash_u32(h, (uint32_t)w->live_count);
  for (int i = 0; i < NG_WORLD_ENTITY_MAX; i++) {
    if (!w->alive[i]) {
      continue;
    }
    h = ng_hash_u32(h, (uint32_t)(i + 1));
    h = ng_hash_u32(h, w->type[i]);
    h = ng_hash_f32(h, w->pos_x[i]);
    h = ng_hash_f32(h, w->pos_y[i]);
    h = ng_hash_f32(h, w->pos_z[i]);
    h = ng_hash_f32(h, w->rot_y[i]);
    h = ng_hash_f32(h, w->phase[i]);
  }
  return h;
}
