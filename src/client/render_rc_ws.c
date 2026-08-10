/*
 * World-space RC geometry + quality. North star: docs/radiance-cascades-3d.md
 *
 * Gateway role: pattern | Scope id: render-rc | Flow id: rc-ws
 * Related: src/client/render.c (seed downsample; sparse fill/SH/resolve)
 * Downstream: res/shaders/rc_ws_fill.fs (slot-meta march)
 * Debug: set debug.render.pass probes|uvw|grid|atlas|irradiance
 *
 * rc-ws flow (Track A + B.1–B.3):
 * 1) ensure → tex_prim + tex_grid + tex_meta/hash; quality → slots/dirs/cascades
 * 2) render.c → far look-at cube; gbuf UVW vs far; downsample depth seed
 * 3) dirty → rebuild_prims+grid; sparse_seed from seed pixels → meta+hash
 * 4) sparse fill/merge/SH (S×dirs); resolve linear-probe hash
 *
 * Track B:
 * B.0–B.2) Dense clipmap baseline — shipped
 * B.3) Screen-seeded sparse hashmap — shipping
 * B.4) Hierarchy/SVO: empty skip; collapse=avg children; split=alloc+fill
 * B.5) Depth+curvature LOD; dirty-only; near tiny/far huge; AO hitch
 *
 * Branches / invariants:
 * - Clip locked to cam.target; far 2×CELL snap (grid/prim bounds).
 * - Prim+grid over far clip; fill uses slot world centers from tex_meta.
 * - Grid 8³ × 4 slots; overflow nearer-to-cell-center. BVH deferred.
 * - Sparse slots ≤ SLOT_MAX; hash open-address; stable cell→slot; OOV freed.
 */
// agent: composer-2.5 | 2026-08-10 | rebuild uniform prim grid | a95114
// agent: composer-2.5 | 2026-08-10 | playbook Track B roadmap | d1e2af
// agent: composer-2.5 | 2026-08-10 | B1 playbook clipmap shipped | 55ab7b
// agent: composer-2.5 | 2026-08-10 | B2 playbook amortize shipped | ee4fd1
// agent: composer-2.5 | 2026-08-10 | playbook B3-B5 sparse hierarchy | 0b0624
// agent: composer-2.5 | 2026-08-10 | B3 sparse hashmap seed slots | e1bc85
// agent: composer-2.5 | 2026-08-10 | B3 seed flip stable slots | 4258ae
#include "render_rc_ws.h"
#include "scene/assets.h"
#include "scene/graph.h"
#include <raymath.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NG_RC_WS_PRIM_FLOATS (NG_RC_WS_PRIM_MAX * NG_RC_WS_PRIM_COLS * 4)
#define NG_RC_WS_GRID_EMPTY 255u
#define NG_RC_WS_GRID_HALF_PAD 1.12f

void ng_rc_ws_init(NgRcWsCtx *ws) {
  if (!ws) {
    return;
  }
  memset(ws, 0, sizeof(*ws));
  ws->origin[0] = -6.0f;
  ws->origin[1] = -0.5f;
  ws->origin[2] = -6.0f;
  ws->size[0] = 12.0f;
  ws->size[1] = 5.5f;
  ws->size[2] = 12.0f;
  ws->probe_n = 128;
  ws->slot_cap = 128;
  ws->dirs = 8;
  ws->cascades = 3;
  ws->steps = 5;
}

void ng_rc_ws_shutdown(NgRcWsCtx *ws) {
  if (!ws) {
    return;
  }
  if (ws->prim_tex_ready) {
    UnloadTexture(ws->tex_prim);
    ws->prim_tex_ready = false;
  }
  if (ws->grid_tex_ready) {
    UnloadTexture(ws->tex_grid);
    ws->grid_tex_ready = false;
  }
  free(ws->prim_rgba);
  ws->prim_rgba = NULL;
  free(ws->grid_rgba);
  ws->grid_rgba = NULL;
  if (ws->sparse_tex_ready) {
    UnloadTexture(ws->tex_meta);
    UnloadTexture(ws->tex_hash);
    ws->sparse_tex_ready = false;
  }
  free(ws->meta_rgba);
  ws->meta_rgba = NULL;
  free(ws->hash_rgba);
  ws->hash_rgba = NULL;
  ws->prim_count = 0;
  ws->slot_count = 0;
  ws->ready = false;
}

/** Allocate prim + grid GPU/CPU scratch. */
static bool ng_rc_ws_alloc(NgRcWsCtx *ws) {
  if (ws->ready && ws->prim_tex_ready && ws->prim_rgba && ws->grid_tex_ready && ws->grid_rgba) {
    return true;
  }
  if (!ws->prim_rgba) {
    ws->prim_rgba = (float *)calloc((size_t)NG_RC_WS_PRIM_FLOATS, sizeof(float));
  }
  if (!ws->grid_rgba) {
    ws->grid_rgba = (unsigned char *)calloc((size_t)NG_RC_WS_GRID_BYTES, 1);
  }
  if (!ws->prim_rgba || !ws->grid_rgba) {
    ng_rc_ws_shutdown(ws);
    return false;
  }
  if (!ws->prim_tex_ready) {
    Image pimg = {0};
    pimg.data = ws->prim_rgba;
    pimg.width = NG_RC_WS_PRIM_COLS;
    pimg.height = NG_RC_WS_PRIM_MAX;
    pimg.mipmaps = 1;
    pimg.format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;
    ws->tex_prim = LoadTextureFromImage(pimg);
    pimg.data = NULL;
    UnloadImage(pimg);
    if (ws->tex_prim.id == 0) {
      ng_rc_ws_shutdown(ws);
      return false;
    }
    SetTextureFilter(ws->tex_prim, TEXTURE_FILTER_POINT);
    SetTextureWrap(ws->tex_prim, TEXTURE_WRAP_CLAMP);
    ws->prim_tex_ready = true;
  }
  if (!ws->grid_tex_ready) {
    memset(ws->grid_rgba, NG_RC_WS_GRID_EMPTY, (size_t)NG_RC_WS_GRID_BYTES);
    Image gimg = {0};
    gimg.data = ws->grid_rgba;
    gimg.width = NG_RC_WS_GRID_RES;
    gimg.height = NG_RC_WS_GRID_RES * NG_RC_WS_GRID_RES;
    gimg.mipmaps = 1;
    gimg.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    ws->tex_grid = LoadTextureFromImage(gimg);
    gimg.data = NULL;
    UnloadImage(gimg);
    if (ws->tex_grid.id == 0) {
      ng_rc_ws_shutdown(ws);
      return false;
    }
    SetTextureFilter(ws->tex_grid, TEXTURE_FILTER_POINT);
    SetTextureWrap(ws->tex_grid, TEXTURE_WRAP_CLAMP);
    ws->grid_tex_ready = true;
  }
  ws->ready = true;
  return true;
}


/** Allocate sparse meta/hash GPU+CPU scratch for slot_cap. */
static bool ng_rc_ws_sparse_alloc(NgRcWsCtx *ws) {
  int cap = ws->slot_cap > 0 ? ws->slot_cap : 128;
  if (cap > NG_RC_WS_SLOT_MAX) {
    cap = NG_RC_WS_SLOT_MAX;
  }
  int hsz = 1;
  while (hsz < cap * 2 && hsz < NG_RC_WS_HASH_MAX) {
    hsz <<= 1;
  }
  if (hsz < 64) {
    hsz = 64;
  }
  const int need_realloc =
      !ws->meta_rgba || !ws->hash_rgba || ws->slot_cap != cap || ws->hash_size != hsz;
  if (need_realloc) {
    if (ws->sparse_tex_ready) {
      UnloadTexture(ws->tex_meta);
      UnloadTexture(ws->tex_hash);
      ws->sparse_tex_ready = false;
    }
    free(ws->meta_rgba);
    free(ws->hash_rgba);
    ws->meta_rgba = (float *)calloc((size_t)cap * 4u, sizeof(float));
    ws->hash_rgba = (float *)calloc((size_t)hsz * 4u, sizeof(float));
    if (!ws->meta_rgba || !ws->hash_rgba) {
      return false;
    }
    ws->slot_cap = cap;
    ws->hash_size = hsz;
    ws->probe_n = cap;
  }
  if (!ws->sparse_tex_ready) {
    Image mimg = {0};
    mimg.data = ws->meta_rgba;
    mimg.width = 1;
    mimg.height = cap;
    mimg.mipmaps = 1;
    mimg.format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;
    ws->tex_meta = LoadTextureFromImage(mimg);
    mimg.data = NULL;
    UnloadImage(mimg);
    Image himg = {0};
    himg.data = ws->hash_rgba;
    himg.width = hsz;
    himg.height = 1;
    himg.mipmaps = 1;
    himg.format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;
    ws->tex_hash = LoadTextureFromImage(himg);
    himg.data = NULL;
    UnloadImage(himg);
    if (ws->tex_meta.id == 0 || ws->tex_hash.id == 0) {
      return false;
    }
    SetTextureFilter(ws->tex_meta, TEXTURE_FILTER_POINT);
    SetTextureFilter(ws->tex_hash, TEXTURE_FILTER_POINT);
    SetTextureWrap(ws->tex_meta, TEXTURE_WRAP_CLAMP);
    SetTextureWrap(ws->tex_hash, TEXTURE_WRAP_CLAMP);
    ws->sparse_tex_ready = true;
  }
  return true;
}

bool ng_rc_ws_ensure(NgRcWsCtx *ws, int quality) {
  static const int k_slots[5] = {128, 192, 256, 384, 512};
  static const int k_dirs[5] = {12, 16, 24, 32, 48};
  static const int k_cascades[5] = {1, 2, 3, 3, 3};
  static const int k_steps[5] = {3, 4, 5, 6, 8};
  if (!ws) {
    return false;
  }
  if (!ng_rc_ws_alloc(ws)) {
    return false;
  }
  int q = quality;
  if (q < 0) {
    q = 0;
  } else if (q > 4) {
    q = 4;
  }
  ws->slot_cap = k_slots[q];
  ws->probe_n = ws->slot_cap;
  ws->dirs = k_dirs[q];
  ws->cascades = k_cascades[q];
  ws->steps = k_steps[q];
  return ng_rc_ws_sparse_alloc(ws);
}

static uint32_t ng_rc_ws_hash_u32(uint32_t h, uint32_t v) {
  h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
  return h;
}

uint32_t ng_rc_ws_scene_hash(void) {
  uint32_t h = 2166136261u;
  h = ng_rc_ws_hash_u32(h, 0xF1004u);
  const int n = mod_scene_graph_inst_count();
  h = ng_rc_ws_hash_u32(h, (uint32_t)n);
  for (int i = 0; i < n; i++) {
    const NgSceneInst *inst = mod_scene_graph_inst_at(i);
    if (!inst || !inst->alive) {
      continue;
    }
    h = ng_rc_ws_hash_u32(h, (uint32_t)(inst->pos[0] * 100.0f));
    h = ng_rc_ws_hash_u32(h, (uint32_t)(inst->pos[1] * 100.0f));
    h = ng_rc_ws_hash_u32(h, (uint32_t)(inst->pos[2] * 100.0f));
    h = ng_rc_ws_hash_u32(h, (uint32_t)(inst->rot[0] * 100.0f));
    h = ng_rc_ws_hash_u32(h, (uint32_t)(inst->rot[1] * 100.0f));
    h = ng_rc_ws_hash_u32(h, (uint32_t)(inst->rot[2] * 100.0f));
    h = ng_rc_ws_hash_u32(h, (uint32_t)(inst->scale * 100.0f));
  }
  return h;
}

/** Build SDF prim from graph inst (pose matches mesh.vs instanceTransform). */
bool ng_rc_ws_inst_prim(int i, NgRcWsPrim *out) {
  if (!out) {
    return false;
  }
  const NgSceneInst *inst = mod_scene_graph_inst_at(i);
  if (!inst || !inst->alive || !inst->model[0]) {
    return false;
  }
  NgSceneResolvedModel resolved;
  if (!mod_scene_assets_resolve_model(inst->model, &resolved) || !resolved.ok) {
    return false;
  }
  const float s = inst->scale > 0.0f ? inst->scale : 1.0f;
  float hx;
  float hy;
  float hz;
  int type;
  if (resolved.mesh_kind == NG_SCENE_MESH_SPHERE) {
    hx = hy = hz = resolved.mesh_w * s;
    type = 1;
  } else {
    const float k = 1.5f * 0.5f;
    hx = resolved.mesh_w * k * s;
    hy = resolved.mesh_h * k * s;
    hz = resolved.mesh_d * k * s;
    type = 0;
  }
  memset(out, 0, sizeof(*out));
  out->center[0] = inst->pos[0];
  out->center[1] = inst->pos[1];
  out->center[2] = inst->pos[2];
  out->half[0] = hx;
  out->half[1] = hy;
  out->half[2] = hz;
  out->type = type;
  {
    const Quaternion q = QuaternionFromEuler(inst->rot[0], inst->rot[1], inst->rot[2]);
    out->quat[0] = q.x;
    out->quat[1] = q.y;
    out->quat[2] = q.z;
    out->quat[3] = q.w;
  }
  const float ar = (resolved.have_tint ? (float)resolved.tint_r : 180.0f) / 255.0f;
  const float ag = (resolved.have_tint ? (float)resolved.tint_g : 180.0f) / 255.0f;
  const float ab = (resolved.have_tint ? (float)resolved.tint_b : 180.0f) / 255.0f;
  const float er = (resolved.have_glow ? (float)resolved.glow_r : 0.0f) / 255.0f;
  const float eg = (resolved.have_glow ? (float)resolved.glow_g : 0.0f) / 255.0f;
  const float eb = (resolved.have_glow ? (float)resolved.glow_b : 0.0f) / 255.0f;
  out->albedo[0] = ar;
  out->albedo[1] = ag;
  out->albedo[2] = ab;
  out->emit[0] = er;
  out->emit[1] = eg;
  out->emit[2] = eb;
  out->roughness = resolved.roughness;
  out->metalness = resolved.metalness;
  const float emit_boost = 2.2f;
  const float bounce = 1.35f;
  out->lit[0] = er * emit_boost + ar * bounce;
  out->lit[1] = eg * emit_boost + ag * bounce;
  out->lit[2] = eb * emit_boost + ab * bounce;
  if (out->lit[0] > 1.0f) {
    out->lit[0] = 1.0f;
  }
  if (out->lit[1] > 1.0f) {
    out->lit[1] = 1.0f;
  }
  if (out->lit[2] > 1.0f) {
    out->lit[2] = 1.0f;
  }
  return true;
}

/** Conservative sphere radius covering oriented box or sphere. */
static float ng_rc_ws_prim_bound_r(const NgRcWsPrim *p) {
  if (p->type == 1) {
    return p->half[0];
  }
  return sqrtf(p->half[0] * p->half[0] + p->half[1] * p->half[1] + p->half[2] * p->half[2]);
}

static bool ng_rc_ws_prim_overlaps_clip(const NgRcWsCtx *ws, const NgRcWsPrim *p) {
  const float r = ng_rc_ws_prim_bound_r(p);
  const float amin[3] = {p->center[0] - r, p->center[1] - r, p->center[2] - r};
  const float amax[3] = {p->center[0] + r, p->center[1] + r, p->center[2] + r};
  const float bmin[3] = {ws->origin[0], ws->origin[1], ws->origin[2]};
  const float bmax[3] = {ws->origin[0] + ws->size[0], ws->origin[1] + ws->size[1],
                         ws->origin[2] + ws->size[2]};
  return !(amin[0] > bmax[0] || amax[0] < bmin[0] || amin[1] > bmax[1] || amax[1] < bmin[1] ||
           amin[2] > bmax[2] || amax[2] < bmin[2]);
}

/** Insert prim into cell slots; if full keep nearer-to-cell-center. */
static void ng_rc_ws_grid_insert(unsigned char *slots, float *dists, int pid, float d2) {
  for (int s = 0; s < NG_RC_WS_GRID_SLOT; s++) {
    if (slots[s] == NG_RC_WS_GRID_EMPTY) {
      slots[s] = (unsigned char)pid;
      dists[s] = d2;
      return;
    }
  }
  int worst = 0;
  for (int s = 1; s < NG_RC_WS_GRID_SLOT; s++) {
    if (dists[s] > dists[worst]) {
      worst = s;
    }
  }
  if (d2 < dists[worst]) {
    slots[worst] = (unsigned char)pid;
    dists[worst] = d2;
  }
}

void ng_rc_ws_rebuild_prims(NgRcWsCtx *ws) {
  if (!ws || !ws->ready || !ws->prim_tex_ready || !ws->prim_rgba) {
    return;
  }
  ws->prim_count = 0;
  memset(ws->prim_rgba, 0, (size_t)NG_RC_WS_PRIM_FLOATS * sizeof(float));
  const int n = mod_scene_graph_inst_count();
  for (int i = 0; i < n && ws->prim_count < NG_RC_WS_PRIM_MAX; i++) {
    NgRcWsPrim p;
    if (!ng_rc_ws_inst_prim(i, &p)) {
      continue;
    }
    if (!ng_rc_ws_prim_overlaps_clip(ws, &p)) {
      continue;
    }
    const int row = ws->prim_count;
    ws->prims[row] = p;
    float *rowf = ws->prim_rgba + row * NG_RC_WS_PRIM_COLS * 4;
    rowf[0] = p.center[0];
    rowf[1] = p.center[1];
    rowf[2] = p.center[2];
    rowf[3] = (float)p.type;
    rowf[4] = p.half[0];
    rowf[5] = p.half[1];
    rowf[6] = p.half[2];
    rowf[7] = ng_rc_ws_prim_bound_r(&p);
    rowf[8] = p.quat[0];
    rowf[9] = p.quat[1];
    rowf[10] = p.quat[2];
    rowf[11] = p.quat[3];
    rowf[12] = p.emit[0];
    rowf[13] = p.emit[1];
    rowf[14] = p.emit[2];
    rowf[15] = p.roughness;
    rowf[16] = p.albedo[0];
    rowf[17] = p.albedo[1];
    rowf[18] = p.albedo[2];
    rowf[19] = p.metalness;
    rowf[20] = p.lit[0];
    rowf[21] = p.lit[1];
    rowf[22] = p.lit[2];
    rowf[23] = 1.0f;
    ws->prim_count++;
  }
  UpdateTexture(ws->tex_prim, ws->prim_rgba);
}

void ng_rc_ws_rebuild_grid(NgRcWsCtx *ws) {
  if (!ws || !ws->ready || !ws->grid_tex_ready || !ws->grid_rgba) {
    return;
  }
  memset(ws->grid_rgba, NG_RC_WS_GRID_EMPTY, (size_t)NG_RC_WS_GRID_BYTES);
  float dists[NG_RC_WS_GRID_CELLS * NG_RC_WS_GRID_SLOT];
  for (int i = 0; i < NG_RC_WS_GRID_CELLS * NG_RC_WS_GRID_SLOT; i++) {
    dists[i] = 1e30f;
  }

  const int gr = NG_RC_WS_GRID_RES;
  const float ox = ws->origin[0];
  const float oy = ws->origin[1];
  const float oz = ws->origin[2];
  const float sx = ws->size[0] > 1e-5f ? ws->size[0] : 1.0f;
  const float sy = ws->size[1] > 1e-5f ? ws->size[1] : 1.0f;
  const float sz = ws->size[2] > 1e-5f ? ws->size[2] : 1.0f;
  const float invx = (float)gr / sx;
  const float invy = (float)gr / sy;
  const float invz = (float)gr / sz;
  const float cellx = sx / (float)gr;
  const float celly = sy / (float)gr;
  const float cellz = sz / (float)gr;

  for (int pi = 0; pi < ws->prim_count; pi++) {
    const NgRcWsPrim *p = &ws->prims[pi];
    const float r = ng_rc_ws_prim_bound_r(p) * NG_RC_WS_GRID_HALF_PAD;
    int x0 = (int)floorf((p->center[0] - r - ox) * invx);
    int y0 = (int)floorf((p->center[1] - r - oy) * invy);
    int z0 = (int)floorf((p->center[2] - r - oz) * invz);
    int x1 = (int)floorf((p->center[0] + r - ox) * invx);
    int y1 = (int)floorf((p->center[1] + r - oy) * invy);
    int z1 = (int)floorf((p->center[2] + r - oz) * invz);
    if (x0 < 0) {
      x0 = 0;
    }
    if (y0 < 0) {
      y0 = 0;
    }
    if (z0 < 0) {
      z0 = 0;
    }
    if (x1 >= gr) {
      x1 = gr - 1;
    }
    if (y1 >= gr) {
      y1 = gr - 1;
    }
    if (z1 >= gr) {
      z1 = gr - 1;
    }
    if (x0 > x1 || y0 > y1 || z0 > z1) {
      continue;
    }
    for (int iz = z0; iz <= z1; iz++) {
      for (int iy = y0; iy <= y1; iy++) {
        for (int ix = x0; ix <= x1; ix++) {
          const int cell = ix + iy * gr + iz * gr * gr;
          const float cx = ox + ((float)ix + 0.5f) * cellx;
          const float cy = oy + ((float)iy + 0.5f) * celly;
          const float cz = oz + ((float)iz + 0.5f) * cellz;
          const float dx = p->center[0] - cx;
          const float dy = p->center[1] - cy;
          const float dz = p->center[2] - cz;
          const float d2 = dx * dx + dy * dy + dz * dz;
          unsigned char *slots = ws->grid_rgba + cell * NG_RC_WS_GRID_SLOT;
          ng_rc_ws_grid_insert(slots, dists + cell * NG_RC_WS_GRID_SLOT, pi, d2);
        }
      }
    }
  }
  UpdateTexture(ws->tex_grid, ws->grid_rgba);
}

/** Mix cell coords into open-address hash. */
static uint32_t ng_rc_ws_cell_hash(int32_t ix, int32_t iy, int32_t iz) {
  uint32_t h = (uint32_t)ix * 73856093u;
  h ^= (uint32_t)iy * 19349663u;
  h ^= (uint32_t)iz * 83492791u;
  return h;
}

/** Index of used slot with cell, or -1. */
static int ng_rc_ws_slot_find(const NgRcWsCtx *ws, int32_t ix, int32_t iy, int32_t iz) {
  for (int i = 0; i < ws->slot_cap; i++) {
    if (ws->slots[i].used && ws->slots[i].ix == ix && ws->slots[i].iy == iy &&
        ws->slots[i].iz == iz) {
      return i;
    }
  }
  return -1;
}

/** Append unique cell into dense wanted[0..*nwant). */
static void ng_rc_ws_want_add(NgRcWsSlot *wanted, int *nwant, int cap, int32_t ix, int32_t iy,
                              int32_t iz) {
  if (*nwant >= cap) {
    return;
  }
  for (int i = 0; i < *nwant; i++) {
    if (wanted[i].ix == ix && wanted[i].iy == iy && wanted[i].iz == iz) {
      return;
    }
  }
  wanted[*nwant].ix = ix;
  wanted[*nwant].iy = iy;
  wanted[*nwant].iz = iz;
  wanted[*nwant].used = 1;
  wanted[*nwant].dirty = 0;
  (*nwant)++;
}

/** First free slot index, or -1. */
static int ng_rc_ws_slot_alloc(NgRcWsCtx *ws) {
  for (int i = 0; i < ws->slot_cap; i++) {
    if (!ws->slots[i].used) {
      return i;
    }
  }
  return -1;
}

/** Write meta row for one slot (xyz center; a=1 clean / 2 dirty). */
static void ng_rc_ws_meta_write(NgRcWsCtx *ws, int i, float cell) {
  float *m = ws->meta_rgba + i * 4;
  if (!ws->slots[i].used) {
    m[0] = m[1] = m[2] = m[3] = 0.0f;
    return;
  }
  m[0] = ((float)ws->slots[i].ix + 0.5f) * cell;
  m[1] = ((float)ws->slots[i].iy + 0.5f) * cell;
  m[2] = ((float)ws->slots[i].iz + 0.5f) * cell;
  m[3] = ws->slots[i].dirty ? 2.0f : 1.0f;
}

static void ng_rc_ws_rebuild_hash(NgRcWsCtx *ws) {
  const int hsz = ws->hash_size;
  const uint32_t mask = (uint32_t)hsz - 1u;
  for (int i = 0; i < hsz; i++) {
    ws->hash_tab[i] = -1;
    float *hr = ws->hash_rgba + i * 4;
    hr[0] = hr[1] = hr[2] = hr[3] = 0.0f;
  }
  for (int i = 0; i < ws->slot_cap; i++) {
    if (!ws->slots[i].used) {
      continue;
    }
    uint32_t h = ng_rc_ws_cell_hash(ws->slots[i].ix, ws->slots[i].iy, ws->slots[i].iz) & mask;
    for (int step = 0; step < hsz; step++) {
      if (ws->hash_tab[h] < 0) {
        ws->hash_tab[h] = i;
        float *hr = ws->hash_rgba + (int)h * 4;
        hr[0] = (float)(i + 1);
        hr[1] = (float)ws->slots[i].ix;
        hr[2] = (float)ws->slots[i].iy;
        hr[3] = (float)ws->slots[i].iz;
        break;
      }
      h = (h + 1u) & mask;
    }
  }
  UpdateTexture(ws->tex_hash, ws->hash_rgba);
}

void ng_rc_ws_sparse_mark_dirty(NgRcWsCtx *ws) {
  if (!ws || !ws->sparse_tex_ready) {
    return;
  }
  const float cell = NG_RC_WS_CELL;
  for (int i = 0; i < ws->slot_cap; i++) {
    if (ws->slots[i].used) {
      ws->slots[i].dirty = 1;
      ng_rc_ws_meta_write(ws, i, cell);
    }
  }
  UpdateTexture(ws->tex_meta, ws->meta_rgba);
}

void ng_rc_ws_sparse_clear_dirty(NgRcWsCtx *ws) {
  if (!ws || !ws->sparse_tex_ready) {
    return;
  }
  const float cell = NG_RC_WS_CELL;
  for (int i = 0; i < ws->slot_cap; i++) {
    if (ws->slots[i].used && ws->slots[i].dirty) {
      ws->slots[i].dirty = 0;
      ng_rc_ws_meta_write(ws, i, cell);
    }
  }
  UpdateTexture(ws->tex_meta, ws->meta_rgba);
}

void ng_rc_ws_sparse_seed(NgRcWsCtx *ws, const unsigned char *rgba, int w, int h,
                          const float origin[3], const float size[3]) {
  // agent: composer-2.5 | 2026-08-10 | B3 retain until OOV seed | f1dc1e
  if (!ws || !rgba || w <= 0 || h <= 0 || !origin || !size) {
    return;
  }
  if (!ng_rc_ws_sparse_alloc(ws)) {
    return;
  }
  const float cell = NG_RC_WS_CELL;
  const int cap = ws->slot_cap < NG_RC_WS_SLOT_MAX ? ws->slot_cap : NG_RC_WS_SLOT_MAX;

  uint8_t still[NG_RC_WS_SLOT_MAX];
  memset(still, 0, (size_t)cap);
  NgRcWsSlot news[NG_RC_WS_SLOT_MAX];
  int nnew = 0;

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      const unsigned char *p = rgba + ((size_t)(y * w + x) * 4u);
      if (p[3] < 128) {
        continue;
      }
      const float u = (float)p[0] / 255.0f;
      const float v = (float)p[1] / 255.0f;
      const float ww = (float)p[2] / 255.0f;
      const float px = origin[0] + u * size[0];
      const float py = origin[1] + v * size[1];
      const float pz = origin[2] + ww * size[2];
      const int32_t ix = (int32_t)floorf(px / cell);
      const int32_t iy = (int32_t)floorf(py / cell);
      const int32_t iz = (int32_t)floorf(pz / cell);
      const int si = ng_rc_ws_slot_find(ws, ix, iy, iz);
      if (si >= 0) {
        still[si] = 1;
      } else {
        ng_rc_ws_want_add(news, &nnew, cap, ix, iy, iz);
      }
    }
  }

  /* Free only OOV; survivors keep index + SH. */
  for (int i = 0; i < ws->slot_cap; i++) {
    if (!ws->slots[i].used) {
      continue;
    }
    if (!still[i]) {
      ws->slots[i].used = 0;
      ws->slots[i].dirty = 0;
    } else {
      ws->slots[i].dirty = 0;
    }
  }

  int nfree = 0;
  for (int i = 0; i < ws->slot_cap; i++) {
    if (!ws->slots[i].used) {
      nfree++;
    }
  }
  /* Admit new surface cells into free slots only (no reshuffle of survivors). */
  if (nnew > nfree) {
    nnew = nfree;
  }
  for (int i = 0; i < nnew; i++) {
    const int si = ng_rc_ws_slot_alloc(ws);
    if (si < 0) {
      break;
    }
    ws->slots[si].ix = news[i].ix;
    ws->slots[si].iy = news[i].iy;
    ws->slots[si].iz = news[i].iz;
    ws->slots[si].used = 1;
    // agent: composer-2.5 | 2026-08-10 | slot dirty on reuse note | 392430
    ws->slots[si].dirty = 1; /* reuse: refill cascade/SH row before resolve */
  }

  /* Face pad into remaining free slots (does not evict). */
  static const int32_t k_face[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                                       {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  for (int i = 0; i < ws->slot_cap; i++) {
    if (!ws->slots[i].used) {
      continue;
    }
    for (int f = 0; f < 6; f++) {
      const int32_t nx = ws->slots[i].ix + k_face[f][0];
      const int32_t ny = ws->slots[i].iy + k_face[f][1];
      const int32_t nz = ws->slots[i].iz + k_face[f][2];
      if (ng_rc_ws_slot_find(ws, nx, ny, nz) >= 0) {
        continue;
      }
      const int si = ng_rc_ws_slot_alloc(ws);
      if (si < 0) {
        goto pad_done;
      }
      ws->slots[si].ix = nx;
      ws->slots[si].iy = ny;
      ws->slots[si].iz = nz;
      ws->slots[si].used = 1;
      ws->slots[si].dirty = 1;
    }
  }
pad_done:

  int count = 0;
  memset(ws->meta_rgba, 0, (size_t)ws->slot_cap * 4u * sizeof(float));
  for (int i = 0; i < ws->slot_cap; i++) {
    ng_rc_ws_meta_write(ws, i, cell);
    if (ws->slots[i].used) {
      count++;
    }
  }
  ws->slot_count = count;
  UpdateTexture(ws->tex_meta, ws->meta_rgba);
  ng_rc_ws_rebuild_hash(ws);
}
// agent: composer-2.5 | 2026-08-10 | rebuild uniform prim grid | a95114
// agent: composer-2.5 | 2026-08-10 | playbook Track B roadmap | d1e2af
// agent: composer-2.5 | 2026-08-10 | B1 playbook clipmap shipped | 55ab7b
// agent: composer-2.5 | 2026-08-10 | B2 playbook amortize shipped | ee4fd1
// agent: composer-2.5 | 2026-08-10 | playbook B3-B5 sparse hierarchy | 0b0624
// agent: composer-2.5 | 2026-08-10 | B3 sparse hashmap seed slots | e1bc85
// agent: composer-2.5 | 2026-08-10 | B3 seed flip stable slots | 4258ae
// agent: composer-2.5 | 2026-08-10 | B3 fair seed incremental fill | 806cdd
// agent: composer-2.5 | 2026-08-10 | B3 retain until OOV seed | f1dc1e
// agent: composer-2.5 | 2026-08-10 | slot dirty on reuse note | 392430
