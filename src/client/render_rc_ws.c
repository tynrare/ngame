/*
 * World-space RC geometry + quality. North star: docs/radiance-cascades-3d.md
 *
 * Gateway role: pattern | Scope id: render-rc | Flow id: rc-ws
 * Related: src/client/render.c (GPU cull, VS draw cull, GPU probes, gbuf)
 * Downstream: res/shaders/rc_ws_cull.fs, rc_ws_inst_vis.fs, rc_ws_probe*.fs, rc_gbuf.vs
 * Debug: set debug.render.pass culling|probes|probes-lod|…
 *
 * rc-ws flow (GPU incremental residency):
 * 1) ensure → tex_prim + tex_bvh + probe RTs (slots/meta/hash/union)
 * 2) scene dirty → prims + inst_i + CPU BVH → upload; GPU clear probe RTs (cold)
 * 3) GPU frustum cull → tex_vis_curr; expand → tex_inst_vis
 * 4) probes (GPU only): vis-union → release → cover → unmet → relax → cover → split → steal
 *    (collapse only if unmet at budget) → steal excess → stats → meta/hash
 * CPU oracle: ng_rc_ws_probe_lazy_tick + tools/rc_ws_probe_smoke (tests/rc_ws)
 * Persist keys; cover-first; no view-move shuffle.
 * 5) gbuf draw: VS samples tex_inst_vis (material-map bind; no CPU filter)
 * 6) swap vis prev←curr; compose/debug sample hash + depth
 *
 * Branches / invariants:
 * - Absolute world keys (lod,ix,iy,iz); no look-at clip; no KD free cubes.
 * - No mesh-owner; release via union AABB + shell empty; budget 50% slot_cap.
 * - No hot-path CPU / LoadImageFromTexture / residency UpdateTexture.
 */
// agent: composer-2.5 | 2026-08-12 | playbook incremental residency | 0f5a93
// agent: grok-4.6 | 2026-08-12 | playbook even split steal | 175820
// agent: grok-4.6 | 2026-08-12 | playbook lazy persistent cover | b407ea
// agent: grok-4.6 | 2026-08-12 | docs CPU oracle smoke | 91ac84
// agent: grok-4.6 | 2026-08-12 | cover-pressure steal oracle | f7fae3
// agent: grok-4.6 | 2026-08-12 | persist cover-first relax | 052e50
// agent: composer-2.5 | 2026-08-12 | docs single-root GPU probes | d0ac59
// agent: composer-2.5 | 2026-08-12 | docs GPU copies surface split | 89f49d
// agent: composer-2.5 | 2026-08-12 | rc-ws playbook GPU probes | 8defb7
// agent: composer-2.5 | 2026-08-11 | GI offline BVH cull foundation | 69e867
// agent: composer-2.5 | 2026-08-11 | octree cover split surface tick | 6ba63e
// agent: composer-2.5 | 2026-08-11 | fair 75pct poorest-mesh split | 844498
// agent: composer-2.5 | 2026-08-11 | surface-area 50pct fair split | b7ee3b
// agent: composer-2.5 | 2026-08-11 | stochastic fair branch split | 9c7027
// agent: composer-2.5 | 2026-08-11 | gen-sync octree waves plus log | 10b75c
// agent: composer-2.5 | 2026-08-11 | unlimit root lod contain AABB | 594f2e
// agent: composer-2.5 | 2026-08-11 | gen-complete lod-locked waves | f34ba6
// agent: composer-2.5 | 2026-08-11 | restore cover then gen wave | 8e1bf4
// agent: composer-2.5 | 2026-08-11 | SDF shell keep surface cells only | a394a2
// agent: composer-2.5 | 2026-08-11 | 50pct budget slots 512 quality | 3cc717
// agent: composer-2.5 | 2026-08-11 | enforce surface budget half pool | de62a7
// agent: composer-2.5 | 2026-08-12 | rebuild_prims store inst_i | 267192
// agent: composer-2.5 | 2026-08-12 | GPU probe tick API | af2c49
#include "render_rc_ws.h"
#include "scene/assets.h"
#include "scene/graph.h"
#include <raymath.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NG_RC_WS_PRIM_FLOATS (NG_RC_WS_PRIM_MAX * NG_RC_WS_PRIM_COLS * 4)
#define NG_RC_WS_GRID_EMPTY 255u
#define NG_RC_WS_GRID_HALF_PAD 1.12f

/* Curvature / continuous score deferred — tick uses distance LOD only. */

// agent: composer-2.5 | 2026-08-11 | B66 always-cover collapse tick | c46cdb
static uint32_t s_b66_dbg_frame;
static uint32_t s_b66_splits;
static uint32_t s_b66_relaxes;
static uint32_t s_b66_inserts;

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
  ws->bvh_root = -1;
  ws->bvh_count = 0;
  // agent: composer-2.5 | 2026-08-11 | B66 always-cover collapse tick | c46cdb
  ws->forward[2] = 1.0f;
  ws->tan_half_fov = 0.414f;
  ws->aspect = 1.0f;
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
  if (ws->bvh_tex_ready) {
    UnloadTexture(ws->tex_bvh);
    ws->bvh_tex_ready = false;
  }
  free(ws->prim_rgba);
  ws->prim_rgba = NULL;
  free(ws->grid_rgba);
  ws->grid_rgba = NULL;
  free(ws->bvh_rgba);
  ws->bvh_rgba = NULL;
  if (ws->sparse_tex_ready) {
    UnloadTexture(ws->tex_meta);
    UnloadTexture(ws->tex_hash);
    ws->sparse_tex_ready = false;
  }
  if (ws->prio_tex_ready) {
    UnloadTexture(ws->tex_prio);
    ws->prio_tex_ready = false;
  }
  free(ws->meta_rgba);
  ws->meta_rgba = NULL;
  free(ws->hash_rgba);
  ws->hash_rgba = NULL;
  free(ws->prio_rgba);
  ws->prio_rgba = NULL;
  ws->prim_count = 0;
  ws->slot_count = 0;
  ws->ready = false;
}

/** Allocate prim + grid + bvh GPU/CPU scratch. */
static bool ng_rc_ws_alloc(NgRcWsCtx *ws) {
  // agent: composer-2.5 | 2026-08-11 | GI offline BVH cull foundation | 69e867
  if (ws->ready && ws->prim_tex_ready && ws->prim_rgba && ws->grid_tex_ready && ws->grid_rgba &&
      ws->bvh_tex_ready && ws->bvh_rgba) {
    return true;
  }
  if (!ws->prim_rgba) {
    ws->prim_rgba = (float *)calloc((size_t)NG_RC_WS_PRIM_FLOATS, sizeof(float));
  }
  if (!ws->grid_rgba) {
    ws->grid_rgba = (unsigned char *)calloc((size_t)NG_RC_WS_GRID_BYTES, 1);
  }
  if (!ws->bvh_rgba) {
    ws->bvh_rgba = (float *)calloc((size_t)NG_RC_WS_BVH_FLOATS, sizeof(float));
  }
  if (!ws->prim_rgba || !ws->grid_rgba || !ws->bvh_rgba) {
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
  if (!ws->bvh_tex_ready) {
    Image bimg = {0};
    bimg.data = ws->bvh_rgba;
    bimg.width = NG_RC_WS_BVH_COLS;
    bimg.height = NG_RC_WS_BVH_MAX;
    bimg.mipmaps = 1;
    bimg.format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;
    ws->tex_bvh = LoadTextureFromImage(bimg);
    bimg.data = NULL;
    UnloadImage(bimg);
    if (ws->tex_bvh.id == 0) {
      ng_rc_ws_shutdown(ws);
      return false;
    }
    SetTextureFilter(ws->tex_bvh, TEXTURE_FILTER_POINT);
    SetTextureWrap(ws->tex_bvh, TEXTURE_WRAP_CLAMP);
    ws->bvh_tex_ready = true;
  }
  ws->ready = true;
  return true;
}


/** Allocate sparse meta/hash/prio GPU+CPU scratch for slot_cap. */
static bool ng_rc_ws_sparse_alloc(NgRcWsCtx *ws) {
  // agent: composer-2.5 | 2026-08-11 | B66 always-cover collapse tick | c46cdb
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
  const int need_realloc = !ws->meta_rgba || !ws->hash_rgba || !ws->prio_rgba ||
                           ws->slot_cap != cap || ws->hash_size != hsz;
  if (need_realloc) {
    if (ws->sparse_tex_ready) {
      UnloadTexture(ws->tex_meta);
      UnloadTexture(ws->tex_hash);
      ws->sparse_tex_ready = false;
    }
    if (ws->prio_tex_ready) {
      UnloadTexture(ws->tex_prio);
      ws->prio_tex_ready = false;
    }
    free(ws->meta_rgba);
    free(ws->hash_rgba);
    free(ws->prio_rgba);
    ws->meta_rgba = (float *)calloc((size_t)cap * 4u, sizeof(float));
    ws->hash_rgba = (float *)calloc((size_t)hsz * 4u, sizeof(float));
    ws->prio_rgba = (float *)calloc((size_t)cap * 4u, sizeof(float));
    if (!ws->meta_rgba || !ws->hash_rgba || !ws->prio_rgba) {
      return false;
    }
    ws->slot_cap = cap;
    ws->hash_size = hsz;
    ws->probe_n = cap;
  }
  if (!ws->sparse_tex_ready) {
#if defined(NG_RC_WS_CPU_ONLY)
    /* Headless smoke: CPU buffers only. */
    (void)0;
#else
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
#endif
  }
  if (!ws->prio_tex_ready) {
#if defined(NG_RC_WS_CPU_ONLY)
    (void)0;
#else
    Image pimg = {0};
    pimg.data = ws->prio_rgba;
    pimg.width = cap;
    pimg.height = 1;
    pimg.mipmaps = 1;
    pimg.format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;
    ws->tex_prio = LoadTextureFromImage(pimg);
    pimg.data = NULL;
    UnloadImage(pimg);
    if (ws->tex_prio.id == 0) {
      return false;
    }
    SetTextureFilter(ws->tex_prio, TEXTURE_FILTER_POINT);
    SetTextureWrap(ws->tex_prio, TEXTURE_WRAP_CLAMP);
    ws->prio_tex_ready = true;
#endif
  }
  return true;
}

bool ng_rc_ws_ensure(NgRcWsCtx *ws, int quality) {
  /* ×2 pool vs prior; q2 (default) = 512. Budget remains 50% in surface_tick. */
  // agent: composer-2.5 | 2026-08-11 | 50pct budget slots 512 quality | 3cc717
  static const int k_slots[5] = {256, 384, 512, 512, 512};
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

#if !NG_RC_WS_GI_OFFLINE
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
#endif

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

static void ng_rc_ws_bvh_rebuild(NgRcWsCtx *ws);

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
#if !NG_RC_WS_GI_OFFLINE
    /* GI path: only prims overlapping look-at clip. Cull foundation: all graph prims
     * (visibility = camera frustum / FAR only). */
    // agent: composer-2.5 | 2026-08-11 | cull prims ignore clip volume | 22f8df
    if (!ng_rc_ws_prim_overlaps_clip(ws, &p)) {
      continue;
    }
#endif
    const int row = ws->prim_count;
    p.inst_i = i;
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
    /* a = inst_i+1 for GPU prim→inst vis expand */
    // agent: composer-2.5 | 2026-08-12 | probe incremental helpers | a22078
    rowf[23] = (float)(i + 1);
    ws->prim_count++;
  }
  UpdateTexture(ws->tex_prim, ws->prim_rgba);
  ng_rc_ws_bvh_rebuild(ws);
  ng_rc_ws_upload_bvh(ws);
#if NG_RC_WS_GI_OFFLINE
  TraceLog(LOG_INFO, "rc-ws prims rebuild count=%d bvh=%d (camera frustum cull, no clip filter)",
           ws->prim_count, ws->bvh_count);
#endif
}

/** Pack CPU BVH nodes into tex_bvh (cols: bmin+left, bmax+right, prim). */
void ng_rc_ws_upload_bvh(NgRcWsCtx *ws) {
  // agent: composer-2.5 | 2026-08-11 | GI offline BVH cull foundation | 69e867
  if (!ws || !ws->bvh_tex_ready || !ws->bvh_rgba) {
    return;
  }
  memset(ws->bvh_rgba, 0, (size_t)NG_RC_WS_BVH_FLOATS * sizeof(float));
  const int n = ws->bvh_count < NG_RC_WS_BVH_MAX ? ws->bvh_count : NG_RC_WS_BVH_MAX;
  for (int i = 0; i < n; i++) {
    const NgRcWsBvhNode *nd = &ws->bvh[i];
    float *row = ws->bvh_rgba + i * NG_RC_WS_BVH_COLS * 4;
    row[0] = nd->bmin[0];
    row[1] = nd->bmin[1];
    row[2] = nd->bmin[2];
    row[3] = (float)nd->left;
    row[4] = nd->bmax[0];
    row[5] = nd->bmax[1];
    row[6] = nd->bmax[2];
    row[7] = (float)nd->right;
    row[8] = (float)nd->prim;
    row[9] = 0.0f;
    row[10] = 0.0f;
    row[11] = 1.0f;
  }
  UpdateTexture(ws->tex_bvh, ws->bvh_rgba);
}

/** World AABB for analytic prim (bound sphere padded). */
static void ng_rc_ws_prim_aabb(const NgRcWsPrim *p, float bmin[3], float bmax[3]) {
  const float r = ng_rc_ws_prim_bound_r(p) * NG_RC_WS_GRID_HALF_PAD;
  bmin[0] = p->center[0] - r;
  bmin[1] = p->center[1] - r;
  bmin[2] = p->center[2] - r;
  bmax[0] = p->center[0] + r;
  bmax[1] = p->center[1] + r;
  bmax[2] = p->center[2] + r;
}

static void ng_rc_ws_aabb_merge(float omin[3], float omax[3], const float amin[3],
                                const float amax[3]) {
  for (int i = 0; i < 3; i++) {
    if (amin[i] < omin[i]) {
      omin[i] = amin[i];
    }
    if (amax[i] > omax[i]) {
      omax[i] = amax[i];
    }
  }
}

/** Centroid-split BVH; returns node index or -1. */
static int ng_rc_ws_bvh_build_range(NgRcWsCtx *ws, int *idx, int n) {
  // agent: composer-2.5 | 2026-08-11 | B6 BVH cache scene dirty | e0cf18
  if (n <= 0 || ws->bvh_count >= NG_RC_WS_BVH_MAX) {
    return -1;
  }
  const int node = ws->bvh_count++;
  NgRcWsBvhNode *nd = &ws->bvh[node];
  nd->left = -1;
  nd->right = -1;
  nd->prim = -1;
  if (n == 1) {
    const int pi = idx[0];
    nd->prim = pi;
    ng_rc_ws_prim_aabb(&ws->prims[pi], nd->bmin, nd->bmax);
    return node;
  }
  float cmin[3] = {1e30f, 1e30f, 1e30f};
  float cmax[3] = {-1e30f, -1e30f, -1e30f};
  for (int i = 0; i < n; i++) {
    float pmin[3], pmax[3];
    ng_rc_ws_prim_aabb(&ws->prims[idx[i]], pmin, pmax);
    if (i == 0) {
      cmin[0] = pmin[0];
      cmin[1] = pmin[1];
      cmin[2] = pmin[2];
      cmax[0] = pmax[0];
      cmax[1] = pmax[1];
      cmax[2] = pmax[2];
    } else {
      ng_rc_ws_aabb_merge(cmin, cmax, pmin, pmax);
    }
  }
  nd->bmin[0] = cmin[0];
  nd->bmin[1] = cmin[1];
  nd->bmin[2] = cmin[2];
  nd->bmax[0] = cmax[0];
  nd->bmax[1] = cmax[1];
  nd->bmax[2] = cmax[2];
  int axis = 0;
  const float ex = cmax[0] - cmin[0];
  const float ey = cmax[1] - cmin[1];
  const float ez = cmax[2] - cmin[2];
  if (ey > ex && ey >= ez) {
    axis = 1;
  } else if (ez > ex && ez >= ey) {
    axis = 2;
  }
  /* Partition by centroid along axis (simple nth swap). */
  for (int i = 0; i < n - 1; i++) {
    for (int j = i + 1; j < n; j++) {
      if (ws->prims[idx[j]].center[axis] < ws->prims[idx[i]].center[axis]) {
        const int t = idx[i];
        idx[i] = idx[j];
        idx[j] = t;
      }
    }
  }
  const int mid = n / 2;
  if (mid <= 0 || mid >= n) {
    nd->prim = idx[0];
    return node;
  }
  nd->left = ng_rc_ws_bvh_build_range(ws, idx, mid);
  nd->right = ng_rc_ws_bvh_build_range(ws, idx + mid, n - mid);
  return node;
}

static void ng_rc_ws_bvh_rebuild(NgRcWsCtx *ws) {
  // agent: composer-2.5 | 2026-08-11 | B6 BVH cache scene dirty | e0cf18
  ws->bvh_count = 0;
  ws->bvh_root = -1;
  if (!ws || ws->prim_count <= 0) {
    return;
  }
  int idx[NG_RC_WS_PRIM_MAX];
  for (int i = 0; i < ws->prim_count; i++) {
    idx[i] = i;
  }
  ws->bvh_root = ng_rc_ws_bvh_build_range(ws, idx, ws->prim_count);
}

void ng_rc_ws_rebuild_grid(NgRcWsCtx *ws) {
  // agent: composer-2.5 | 2026-08-11 | B6 dist LOD split steal | 992c2a
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
  const float cellx = sx / (float)gr;
  const float celly = sy / (float)gr;
  const float cellz = sz / (float)gr;
  const float invx = 1.0f / cellx;
  const float invy = 1.0f / celly;
  const float invz = 1.0f / cellz;

  /* Prim→AABB stamp (cheap); BVH kept for later SDF, not 512 cell queries. */
  for (int pi = 0; pi < ws->prim_count; pi++) {
    float bmin[3], bmax[3];
    ng_rc_ws_prim_aabb(&ws->prims[pi], bmin, bmax);
    int ix0 = (int)floorf((bmin[0] - ox) * invx);
    int iy0 = (int)floorf((bmin[1] - oy) * invy);
    int iz0 = (int)floorf((bmin[2] - oz) * invz);
    int ix1 = (int)floorf((bmax[0] - ox) * invx);
    int iy1 = (int)floorf((bmax[1] - oy) * invy);
    int iz1 = (int)floorf((bmax[2] - oz) * invz);
    if (ix0 < 0) {
      ix0 = 0;
    }
    if (iy0 < 0) {
      iy0 = 0;
    }
    if (iz0 < 0) {
      iz0 = 0;
    }
    if (ix1 >= gr) {
      ix1 = gr - 1;
    }
    if (iy1 >= gr) {
      iy1 = gr - 1;
    }
    if (iz1 >= gr) {
      iz1 = gr - 1;
    }
    if (ix0 > ix1 || iy0 > iy1 || iz0 > iz1) {
      continue;
    }
    const NgRcWsPrim *p = &ws->prims[pi];
    for (int iz = iz0; iz <= iz1; iz++) {
      for (int iy = iy0; iy <= iy1; iy++) {
        for (int ix = ix0; ix <= ix1; ix++) {
          const int cell = ix + iy * gr + iz * gr * gr;
          const float cx = ox + ((float)ix + 0.5f) * cellx;
          const float cy = oy + ((float)iy + 0.5f) * celly;
          const float cz = oz + ((float)iz + 0.5f) * cellz;
          const float dx = p->center[0] - cx;
          const float dy = p->center[1] - cy;
          const float dz = p->center[2] - cz;
          ng_rc_ws_grid_insert(ws->grid_rgba + cell * NG_RC_WS_GRID_SLOT,
                               dists + cell * NG_RC_WS_GRID_SLOT, pi,
                               dx * dx + dy * dy + dz * dz);
        }
      }
    }
  }
  UpdateTexture(ws->tex_grid, ws->grid_rgba);
}

/* Enter radii (m from look-at); LOD L covers up to r[L]. */
static const float k_lod_enter[NG_RC_WS_LOD_MAX] = {3.0f,  6.0f,   12.0f,  24.0f,
                                                     48.0f, 96.0f, 192.0f, 1.0e6f};

float ng_rc_ws_cell_size(int lod) {
  // agent: composer-2.5 | 2026-08-11 | unlimit root lod contain AABB | 594f2e
  int L = lod;
  if (L < 0) {
    L = 0;
  } else if (L > NG_RC_WS_LOD_SOFT_MAX) {
    L = NG_RC_WS_LOD_SOFT_MAX;
  }
  return NG_RC_WS_CELL * ldexpf(1.0f, L);
}

float ng_rc_ws_lod_enter(int lod) {
  int L = lod;
  if (L < 0) {
    L = 0;
  } else if (L >= NG_RC_WS_LOD_MAX) {
    L = NG_RC_WS_LOD_MAX - 1;
  }
  return k_lod_enter[L];
}

int ng_rc_ws_lod_for_dist(float dist) {
  const float d = dist < 0.0f ? 0.0f : dist;
  for (int L = 0; L < NG_RC_WS_LOD_MAX; L++) {
    if (d <= k_lod_enter[L]) {
      return L;
    }
  }
  return NG_RC_WS_LOD_MAX - 1;
}

/** Mix lod+cell into open-address hash. */
static uint32_t ng_rc_ws_cell_hash(uint8_t lod, int32_t ix, int32_t iy, int32_t iz) {
  uint32_t h = (uint32_t)lod * 2654435761u;
  h ^= (uint32_t)ix * 73856093u;
  h ^= (uint32_t)iy * 19349663u;
  h ^= (uint32_t)iz * 83492791u;
  return h;
}

static int ng_rc_ws_leaf_in_aabb(const NgRcWsCtx *ws, int si) {
  if (!ws || si < 0 || !ws->slots[si].used) {
    return 0;
  }
  const float cell = ng_rc_ws_cell_size((int)ws->slots[si].lod);
  const float x0 = (float)ws->slots[si].ix * cell;
  const float y0 = (float)ws->slots[si].iy * cell;
  const float z0 = (float)ws->slots[si].iz * cell;
  const float x1 = x0 + cell;
  const float y1 = y0 + cell;
  const float z1 = z0 + cell;
  const float amin[3] = {ws->origin[0], ws->origin[1], ws->origin[2]};
  const float amax[3] = {ws->origin[0] + ws->size[0], ws->origin[1] + ws->size[1],
                         ws->origin[2] + ws->size[2]};
  return !(x1 < amin[0] || x0 > amax[0] || y1 < amin[1] || y0 > amax[1] || z1 < amin[2] ||
           z0 > amax[2]);
}

// agent: composer-2.5 | 2026-08-11 | B66 always-cover collapse tick | c46cdb
/** Eye distance to leaf center (meters). */
static float ng_rc_ws_leaf_dist(const NgRcWsCtx *ws, int si, const float eye[3]) {
  const float cell = ng_rc_ws_cell_size((int)ws->slots[si].lod);
  const float cx = ((float)ws->slots[si].ix + 0.5f) * cell;
  const float cy = ((float)ws->slots[si].iy + 0.5f) * cell;
  const float cz = ((float)ws->slots[si].iz + 0.5f) * cell;
  const float dx = cx - eye[0];
  const float dy = cy - eye[1];
  const float dz = cz - eye[2];
  return sqrtf(dx * dx + dy * dy + dz * dz);
}

/** Covering leaf via slot scan (no hash — safe mid-tick). */
static int ng_rc_ws_find_covering_scan(const NgRcWsCtx *ws, float px, float py, float pz) {
  int best = -1;
  int best_lod = NG_RC_WS_LOD_MAX;
  for (int i = 0; i < ws->slot_cap; i++) {
    if (!ws->slots[i].used) {
      continue;
    }
    const int L = (int)ws->slots[i].lod;
    const float c = ng_rc_ws_cell_size(L);
    if ((int32_t)floorf(px / c) != ws->slots[i].ix || (int32_t)floorf(py / c) != ws->slots[i].iy ||
        (int32_t)floorf(pz / c) != ws->slots[i].iz) {
      continue;
    }
    if (L < best_lod) {
      best = i;
      best_lod = L;
    }
  }
  return best;
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

static int ng_rc_ws_slot_count_free(const NgRcWsCtx *ws) {
  int n = 0;
  for (int i = 0; i < ws->slot_cap; i++) {
    if (!ws->slots[i].used) {
      n++;
    }
  }
  return n;
}

/** Write meta: xyz center; a = (dirty?20:10)+lod. */
static void ng_rc_ws_meta_write(NgRcWsCtx *ws, int i) {
  float *m = ws->meta_rgba + i * 4;
  if (!ws->slots[i].used) {
    m[0] = m[1] = m[2] = m[3] = 0.0f;
    return;
  }
  const float cell = ng_rc_ws_cell_size((int)ws->slots[i].lod);
  m[0] = ((float)ws->slots[i].ix + 0.5f) * cell;
  m[1] = ((float)ws->slots[i].iy + 0.5f) * cell;
  m[2] = ((float)ws->slots[i].iz + 0.5f) * cell;
  const float lod = (float)ws->slots[i].lod;
  m[3] = (ws->slots[i].dirty ? 20.0f : 10.0f) + lod;
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
    const uint8_t lod = ws->slots[i].lod;
    uint32_t h = ng_rc_ws_cell_hash(lod, ws->slots[i].ix, ws->slots[i].iy, ws->slots[i].iz) & mask;
    for (int step = 0; step < hsz; step++) {
      if (ws->hash_tab[h] < 0) {
        ws->hash_tab[h] = i;
        float *hr = ws->hash_rgba + (int)h * 4;
        hr[0] = (float)((i + 1) + (int)lod * NG_RC_WS_HASH_LOD_STRIDE);
        hr[1] = (float)ws->slots[i].ix;
        hr[2] = (float)ws->slots[i].iy;
        hr[3] = (float)ws->slots[i].iz;
        break;
      }
      h = (h + 1u) & mask;
    }
  }
  if (ws->sparse_tex_ready) {
    UpdateTexture(ws->tex_hash, ws->hash_rgba);
  }
}

static void ng_rc_ws_meta_upload_all(NgRcWsCtx *ws) {
  int count = 0;
  memset(ws->meta_rgba, 0, (size_t)ws->slot_cap * 4u * sizeof(float));
  for (int i = 0; i < ws->slot_cap; i++) {
    ng_rc_ws_meta_write(ws, i);
    if (ws->slots[i].used) {
      count++;
    }
  }
  ws->slot_count = count;
  if (ws->sparse_tex_ready) {
    UpdateTexture(ws->tex_meta, ws->meta_rgba);
  }
  ng_rc_ws_rebuild_hash(ws);
}

/** Leaf center world position. */
static void ng_rc_ws_leaf_center(const NgRcWsCtx *ws, int si, float out[3]) {
  const float cell = ng_rc_ws_cell_size((int)ws->slots[si].lod);
  out[0] = ((float)ws->slots[si].ix + 0.5f) * cell;
  out[1] = ((float)ws->slots[si].iy + 0.5f) * cell;
  out[2] = ((float)ws->slots[si].iz + 0.5f) * cell;
}

/**
 * View test at world point: 1=in frustum cone, 0=outside sides, -1=behind.
 */
static int ng_rc_ws_point_view(const NgRcWsCtx *ws, const float c[3], const float eye[3]) {
  float fx = ws->forward[0];
  float fy = ws->forward[1];
  float fz = ws->forward[2];
  float fl = sqrtf(fx * fx + fy * fy + fz * fz);
  if (fl < 1e-6f) {
    fx = 0.0f;
    fy = 0.0f;
    fz = 1.0f;
    fl = 1.0f;
  }
  fx /= fl;
  fy /= fl;
  fz /= fl;
  const float vx = c[0] - eye[0];
  const float vy = c[1] - eye[1];
  const float vz = c[2] - eye[2];
  const float along = vx * fx + vy * fy + vz * fz;
  if (along <= 0.05f) {
    return -1;
  }
  float rx = 0.0f;
  float ry = 1.0f;
  float rz = 0.0f;
  if (fabsf(fy) > 0.9f) {
    rx = 1.0f;
    ry = 0.0f;
    rz = 0.0f;
  }
  float cx = fy * rz - fz * ry;
  float cy = fz * rx - fx * rz;
  float cz = fx * ry - fy * rx;
  float cl = sqrtf(cx * cx + cy * cy + cz * cz);
  if (cl < 1e-6f) {
    return 1;
  }
  cx /= cl;
  cy /= cl;
  cz /= cl;
  const float ux = cy * fz - cz * fy;
  const float uy = cz * fx - cx * fz;
  const float uz = cx * fy - cy * fx;
  const float x = vx * cx + vy * cy + vz * cz;
  const float y = vx * ux + vy * uy + vz * uz;
  float thv = ws->tan_half_fov;
  if (thv < 0.05f) {
    thv = 0.414f;
  }
  float asp = ws->aspect;
  if (asp < 0.1f) {
    asp = 1.0f;
  }
  const float lim_y = along * thv;
  const float lim_x = lim_y * asp;
  if (fabsf(x) > lim_x * 1.05f || fabsf(y) > lim_y * 1.05f) {
    return 0;
  }
  return 1;
}

/** View test for used leaf center. */
static int ng_rc_ws_leaf_view(const NgRcWsCtx *ws, int si, const float eye[3]) {
  float c[3];
  ng_rc_ws_leaf_center(ws, si, c);
  return ng_rc_ws_point_view(ws, c, eye);
}

void ng_rc_ws_set_view(NgRcWsCtx *ws, const float forward[3], float tan_half_fov, float aspect) {
  // agent: composer-2.5 | 2026-08-11 | B66 always-cover collapse tick | c46cdb
  if (!ws || !forward) {
    return;
  }
  ws->forward[0] = forward[0];
  ws->forward[1] = forward[1];
  ws->forward[2] = forward[2];
  ws->tan_half_fov = tan_half_fov > 0.05f ? tan_half_fov : 0.414f;
  ws->aspect = aspect > 0.1f ? aspect : 1.0f;
}

/** True if slot cell lies inside parent cell (plod,px,py,pz). */
static int ng_rc_ws_inside_parent(const NgRcWsCtx *ws, int si, int plod, int32_t px, int32_t py,
                                  int32_t pz) {
  if (!ws->slots[si].used) {
    return 0;
  }
  const int L = (int)ws->slots[si].lod;
  if (L > plod) {
    return 0;
  }
  const int shift = plod - L;
  return (ws->slots[si].ix >> shift) == px && (ws->slots[si].iy >> shift) == py &&
         (ws->slots[si].iz >> shift) == pz;
}

/**
 * Score: R=promote G=coarsen B=slot+1 A=flags (1 used,+2 coarsen_boost,+4 promote).
 * Promote = too-coarse vs want(d); coarsen ∝ fine depth (more splits → dispose first).
 */
static void ng_rc_ws_score_prio(NgRcWsCtx *ws, const float eye[3], int upload_tex) {
  // agent: composer-2.5 | 2026-08-11 | B66 want-have balanced depth score | 4f6d2d
  memset(ws->prio_rgba, 0, (size_t)ws->slot_cap * 4u * sizeof(float));
  for (int i = 0; i < ws->slot_cap; i++) {
    float *p = ws->prio_rgba + i * 4;
    p[2] = (float)(i + 1);
    if (!ws->slots[i].used) {
      continue;
    }
    const float d = ng_rc_ws_leaf_dist(ws, i, eye);
    const int lod = (int)ws->slots[i].lod;
    const int want = ng_rc_ws_lod_for_dist(d);
    const int in_clip = ng_rc_ws_leaf_in_aabb(ws, i);
    const int view = ng_rc_ws_leaf_view(ws, i, eye);
    const int can_coarsen = (!in_clip) || (lod < NG_RC_WS_LOD_MAX - 1);
    const int fine = NG_RC_WS_LOD_MAX - 1 - lod; /* lod0 → 7 */
    float flags = 1.0f;
    float promote = 0.0f;
    float coarsen = 0.0f;

    if (can_coarsen) {
      /* Dispose priority ∝ detail depth; over-fine vs want adds more. */
      coarsen = (float)(fine * fine) * 100.0f + d * 0.05f + 0.01f;
      if (lod < want) {
        coarsen += (float)(want - lod) * 2000.0f;
      }
      if (!in_clip) {
        coarsen += 3.0e6f;
        flags += 2.0f;
      } else if (view < 0) {
        coarsen += 2.0e5f;
        flags += 2.0f;
      } else if (view == 0) {
        coarsen += 5.0e4f;
        flags += 2.0f;
      }
    }
    /* Only promote when coarser than distance want (lod higher = coarser). */
    if (in_clip && view == 1 && lod > want && lod > 0) {
      promote = (float)(lod - want) / (d + 0.5f);
      flags += 4.0f;
    }
    p[0] = promote;
    p[1] = coarsen;
    p[3] = flags;
  }
  if (upload_tex && ws->prio_tex_ready) {
    UpdateTexture(ws->tex_prio, ws->prio_rgba);
  }
}

static int ng_rc_ws_topk(const NgRcWsCtx *ws, int promote, int *out, int kmax) {
  int n = 0;
  uint8_t taken[NG_RC_WS_SLOT_MAX];
  memset(taken, 0, (size_t)ws->slot_cap);
  for (int t = 0; t < kmax; t++) {
    int best = -1;
    float best_s = -1.0f;
    for (int i = 0; i < ws->slot_cap; i++) {
      if (taken[i] || !ws->slots[i].used) {
        continue;
      }
      const float *p = ws->prio_rgba + i * 4;
      if (p[3] < 0.5f) {
        continue;
      }
      float s;
      if (promote) {
        if (p[3] < 5.0f || ws->slots[i].lod == 0) {
          continue;
        }
        s = p[0];
      } else {
        s = p[1];
      }
      if (s > best_s) {
        best_s = s;
        best = i;
      }
    }
    if (best < 0 || best_s <= 0.0f) {
      break;
    }
    taken[best] = 1;
    out[n++] = best;
  }
  return n;
}

static int ng_rc_ws_apply_split(NgRcWsCtx *ws, int i) {
  if (i < 0 || !ws->slots[i].used || ws->slots[i].lod == 0) {
    return 0;
  }
  if (ng_rc_ws_slot_count_free(ws) < 7) {
    return 0;
  }
  const int child_lod = (int)ws->slots[i].lod - 1;
  const int32_t bx = ws->slots[i].ix * 2;
  const int32_t by = ws->slots[i].iy * 2;
  const int32_t bz = ws->slots[i].iz * 2;
  const NgRcWsSlot saved = ws->slots[i];
  ws->slots[i].used = 0;
  ws->slots[i].dirty = 0;

  int kids[8];
  for (int c = 0; c < 8; c++) {
    kids[c] = ng_rc_ws_slot_alloc(ws);
    if (kids[c] < 0) {
      for (int k = 0; k < c; k++) {
        ws->slots[kids[k]].used = 0;
        ws->slots[kids[k]].dirty = 0;
      }
      ws->slots[i] = saved;
      return 0;
    }
    ws->slots[kids[c]].used = 1; /* reserve so next alloc differs */
  }
  for (int c = 0; c < 8; c++) {
    const int si = kids[c];
    ws->slots[si].lod = (uint8_t)child_lod;
    ws->slots[si].ix = bx + (c & 1);
    ws->slots[si].iy = by + ((c >> 1) & 1);
    ws->slots[si].iz = bz + ((c >> 2) & 1);
    ws->slots[si].dirty = 1;
    ws->slots[si].pad = 0;
  }
  s_b66_splits++;
  return 1;
}

/**
 * Merge siblings under parent → one coarser leaf (coverage preserved).
 * Child octants must be same-lod siblings or uncovered (no foreign leaf).
 * Outside clip at max lod: free.
 */
static int ng_rc_ws_apply_collapse(NgRcWsCtx *ws, int i) {
  // agent: composer-2.5 | 2026-08-11 | B66 always-cover collapse tick | c46cdb
  if (i < 0 || !ws->slots[i].used) {
    return 0;
  }
  const int lod = (int)ws->slots[i].lod;
  const int in_clip = ng_rc_ws_leaf_in_aabb(ws, i);

  if (lod >= NG_RC_WS_LOD_MAX - 1) {
    if (in_clip) {
      return 0;
    }
    ws->slots[i].used = 0;
    ws->slots[i].dirty = 0;
    ws->slots[i].pad = 0;
    s_b66_relaxes++;
    return 1;
  }

  const int plod = lod + 1;
  const int32_t px = ws->slots[i].ix >> 1;
  const int32_t py = ws->slots[i].iy >> 1;
  const int32_t pz = ws->slots[i].iz >> 1;
  const float ccell = ng_rc_ws_cell_size(lod);

  for (int j = 0; j < ws->slot_cap; j++) {
    if (!ng_rc_ws_inside_parent(ws, j, plod, px, py, pz)) {
      continue;
    }
    if ((int)ws->slots[j].lod < lod) {
      return 0;
    }
    if ((int)ws->slots[j].lod == plod) {
      return 0;
    }
  }

  int sib[8];
  int nsib = 0;
  uint8_t have[8];
  memset(have, 0, sizeof(have));
  for (int j = 0; j < ws->slot_cap; j++) {
    if (!ws->slots[j].used || (int)ws->slots[j].lod != lod) {
      continue;
    }
    if ((ws->slots[j].ix >> 1) != px || (ws->slots[j].iy >> 1) != py ||
        (ws->slots[j].iz >> 1) != pz) {
      continue;
    }
    const int oct = (ws->slots[j].ix & 1) | ((ws->slots[j].iy & 1) << 1) |
                    ((ws->slots[j].iz & 1) << 2);
    have[oct] = 1;
    if (nsib < 8) {
      sib[nsib++] = j;
    }
  }
  if (nsib < 1) {
    return 0;
  }

  for (int oct = 0; oct < 8; oct++) {
    if (have[oct]) {
      continue;
    }
    const int32_t cx = px * 2 + (oct & 1);
    const int32_t cy = py * 2 + ((oct >> 1) & 1);
    const int32_t cz = pz * 2 + ((oct >> 2) & 1);
    const float p[3] = {((float)cx + 0.5f) * ccell, ((float)cy + 0.5f) * ccell,
                        ((float)cz + 0.5f) * ccell};
    if (ng_rc_ws_find_covering_scan(ws, p[0], p[1], p[2]) >= 0) {
      return 0;
    }
  }

  for (int s = 0; s < nsib; s++) {
    ws->slots[sib[s]].used = 0;
    ws->slots[sib[s]].dirty = 0;
    ws->slots[sib[s]].pad = 0;
  }

  if (ng_rc_ws_slot_count_free(ws) < 1) {
    for (int s = 0; s < nsib; s++) {
      ws->slots[sib[s]].used = 1;
    }
    return 0;
  }
  const int parent_si = ng_rc_ws_slot_alloc(ws);
  if (parent_si < 0) {
    for (int s = 0; s < nsib; s++) {
      ws->slots[sib[s]].used = 1;
    }
    return 0;
  }
  ws->slots[parent_si].lod = (uint8_t)plod;
  ws->slots[parent_si].ix = px;
  ws->slots[parent_si].iy = py;
  ws->slots[parent_si].iz = pz;
  ws->slots[parent_si].used = 1;
  ws->slots[parent_si].dirty = 1;
  ws->slots[parent_si].pad = 0;

  s_b66_relaxes++;
  return 1;
}

static int ng_rc_ws_apply_insert_cell(NgRcWsCtx *ws, uint8_t lod, int32_t ix, int32_t iy,
                                      int32_t iz) {
  const float cell = ng_rc_ws_cell_size((int)lod);
  const float px = ((float)ix + 0.5f) * cell;
  const float py = ((float)iy + 0.5f) * cell;
  const float pz = ((float)iz + 0.5f) * cell;
  if (ng_rc_ws_find_covering_scan(ws, px, py, pz) >= 0) {
    return 0;
  }
  if (ng_rc_ws_slot_count_free(ws) < 1) {
    return 0;
  }
  const int si = ng_rc_ws_slot_alloc(ws);
  if (si < 0) {
    return 0;
  }
  ws->slots[si].lod = lod;
  ws->slots[si].ix = ix;
  ws->slots[si].iy = iy;
  ws->slots[si].iz = iz;
  ws->slots[si].used = 1;
  ws->slots[si].dirty = 1;
  ws->slots[si].pad = 0;
  s_b66_inserts++;
  return 1;
}

static int ng_rc_ws_cover_lod(const NgRcWsCtx *ws) {
  // agent: composer-2.5 | 2026-08-11 | B66 always-cover collapse tick | c46cdb
  const int budget = (ws->slot_cap * 5) / 8; /* ~62% cover; leave room to refine */
  int best = NG_RC_WS_LOD_MAX - 1;
  for (int L = NG_RC_WS_LOD_MAX - 1; L >= 1; L--) {
    const float cell = ng_rc_ws_cell_size(L);
    if (cell < 1e-5f) {
      continue;
    }
    const int32_t ix0 = (int32_t)floorf(ws->origin[0] / cell);
    const int32_t iy0 = (int32_t)floorf(ws->origin[1] / cell);
    const int32_t iz0 = (int32_t)floorf(ws->origin[2] / cell);
    const int32_t ix1 = (int32_t)floorf((ws->origin[0] + ws->size[0]) / cell);
    const int32_t iy1 = (int32_t)floorf((ws->origin[1] + ws->size[1]) / cell);
    const int32_t iz1 = (int32_t)floorf((ws->origin[2] + ws->size[2]) / cell);
    const int nx = (int)(ix1 - ix0 + 1);
    const int ny = (int)(iy1 - iy0 + 1);
    const int nz = (int)(iz1 - iz0 + 1);
    const int n = nx * ny * nz;
    if (n <= budget) {
      best = L;
    } else {
      break;
    }
  }
  return best;
}

/* forward */
static int ng_rc_ws_collapse_one(NgRcWsCtx *ws, const int *ids, int n, int *ci);

/** Fill every clip-AABB hole at cover LOD (full volume always covered). */
static int ng_rc_ws_cover_clip(NgRcWsCtx *ws, const float eye[3], int *ops) {
  // agent: composer-2.5 | 2026-08-11 | B66 always-cover collapse tick | c46cdb
  const int clod = ng_rc_ws_cover_lod(ws);
  const float cell = ng_rc_ws_cell_size(clod);
  if (cell < 1e-5f) {
    return 0;
  }
  const int32_t ix0 = (int32_t)floorf(ws->origin[0] / cell);
  const int32_t iy0 = (int32_t)floorf(ws->origin[1] / cell);
  const int32_t iz0 = (int32_t)floorf(ws->origin[2] / cell);
  const int32_t ix1 = (int32_t)floorf((ws->origin[0] + ws->size[0]) / cell);
  const int32_t iy1 = (int32_t)floorf((ws->origin[1] + ws->size[1]) / cell);
  const int32_t iz1 = (int32_t)floorf((ws->origin[2] + ws->size[2]) / cell);
  int changed = 0;

  for (int32_t iz = iz0; iz <= iz1 && *ops < NG_RC_WS_PRIO_K; iz++) {
    for (int32_t iy = iy0; iy <= iy1 && *ops < NG_RC_WS_PRIO_K; iy++) {
      for (int32_t ix = ix0; ix <= ix1 && *ops < NG_RC_WS_PRIO_K; ix++) {
        const float px = ((float)ix + 0.5f) * cell;
        const float py = ((float)iy + 0.5f) * cell;
        const float pz = ((float)iz + 0.5f) * cell;
        if (ng_rc_ws_find_covering_scan(ws, px, py, pz) >= 0) {
          continue;
        }
        while (ng_rc_ws_slot_count_free(ws) < 1 && *ops < NG_RC_WS_PRIO_K) {
          int ci = 0;
          if (!ng_rc_ws_collapse_one(ws, NULL, 0, &ci)) {
            return changed;
          }
          (*ops)++;
          changed = 1;
        }
        if (ng_rc_ws_apply_insert_cell(ws, (uint8_t)clod, ix, iy, iz)) {
          (*ops)++;
          changed = 1;
        }
      }
    }
  }
  return changed;
}

/** Collapse one octet: scored ids first, then all leaves by coarsen score. */
static int ng_rc_ws_collapse_one(NgRcWsCtx *ws, const int *ids, int n, int *ci) {
  while (ids && *ci < n) {
    const int id = ids[(*ci)++];
    if (ws->slots[id].used && ng_rc_ws_apply_collapse(ws, id)) {
      return 1;
    }
  }
  uint8_t tried[NG_RC_WS_SLOT_MAX];
  memset(tried, 0, (size_t)ws->slot_cap);
  for (;;) {
    int best = -1;
    float best_s = -1.0f;
    for (int i = 0; i < ws->slot_cap; i++) {
      if (tried[i] || !ws->slots[i].used) {
        continue;
      }
      const float s = ws->prio_rgba[i * 4 + 1];
      if (s > best_s) {
        best_s = s;
        best = i;
      }
    }
    if (best < 0 || best_s <= 0.0f) {
      return 0;
    }
    tried[best] = 1;
    if (ng_rc_ws_apply_collapse(ws, best)) {
      return 1;
    }
  }
}

/**
 * Interleaved depth rebalance: coarsen over-fine/far for reserve+fuel, then split too-coarse.
 * Never deletes in-clip coverage.
 */
static int ng_rc_ws_rebalance(NgRcWsCtx *ws, const float eye[3], int *ops) {
  // agent: composer-2.5 | 2026-08-11 | B66 want-have balanced depth score | 4f6d2d
  const int reserve = ws->slot_cap / 4;
  int changed = 0;

  ng_rc_ws_score_prio(ws, eye, 1);

  int promote_ids[NG_RC_WS_PRIO_K];
  int coarsen_ids[NG_RC_WS_PRIO_K];
  const int np = ng_rc_ws_topk(ws, 1, promote_ids, NG_RC_WS_PRIO_K);
  const int nc = ng_rc_ws_topk(ws, 0, coarsen_ids, NG_RC_WS_PRIO_K);
  int pi = 0;
  int ci = 0;

  while (*ops < NG_RC_WS_PRIO_K) {
    const int nfree = ng_rc_ws_slot_count_free(ws);
    const int need_fuel = (nfree < reserve) || (pi < np && nfree < 7);
    int over_fine = 0;
    /* Peek next coarsen cand: only dispose when healthier-than-want (not merely fine). */
    while (ci < nc && !ws->slots[coarsen_ids[ci]].used) {
      ci++;
    }
    if (ci < nc) {
      const int cid = coarsen_ids[ci];
      const float d = ng_rc_ws_leaf_dist(ws, cid, eye);
      const int lod = (int)ws->slots[cid].lod;
      const int want = ng_rc_ws_lod_for_dist(d);
      if (lod < want) {
        over_fine = 1;
      }
    }

    if (need_fuel || over_fine) {
      if (ng_rc_ws_collapse_one(ws, coarsen_ids, nc, &ci)) {
        (*ops)++;
        changed = 1;
        continue;
      }
      if (need_fuel) {
        break;
      }
      /* over_fine list exhausted — fall through to split */
    }

    if (pi >= np || nfree < 7) {
      break;
    }
    {
      const int id = promote_ids[pi++];
      if (!ws->slots[id].used || ws->slots[id].lod == 0) {
        continue;
      }
      if (ng_rc_ws_apply_split(ws, id)) {
        (*ops)++;
        changed = 1;
      }
    }
  }
  return changed;
}

void ng_rc_ws_sparse_mark_dirty(NgRcWsCtx *ws) {
  if (!ws || !ws->sparse_tex_ready) {
    return;
  }
  for (int i = 0; i < ws->slot_cap; i++) {
    if (ws->slots[i].used) {
      ws->slots[i].dirty = 1;
      ng_rc_ws_meta_write(ws, i);
    }
  }
  UpdateTexture(ws->tex_meta, ws->meta_rgba);
}

void ng_rc_ws_sparse_clear_dirty(NgRcWsCtx *ws) {
  if (!ws || !ws->sparse_tex_ready) {
    return;
  }
  for (int i = 0; i < ws->slot_cap; i++) {
    if (ws->slots[i].used && ws->slots[i].dirty) {
      ws->slots[i].dirty = 0;
      ng_rc_ws_meta_write(ws, i);
    }
  }
  UpdateTexture(ws->tex_meta, ws->meta_rgba);
}

int ng_rc_ws_dirty_count(const NgRcWsCtx *ws) {
  if (!ws) {
    return 0;
  }
  int n = 0;
  for (int i = 0; i < ws->slot_cap; i++) {
    if (ws->slots[i].used && ws->slots[i].dirty) {
      n++;
    }
  }
  return n;
}

void ng_rc_ws_prio_select(const NgRcWsCtx *ws, int *promote_ids, int *np, int *relax_ids, int *nr,
                          int kmax) {
  if (!ws || !promote_ids || !relax_ids || !np || !nr) {
    return;
  }
  *np = ng_rc_ws_topk(ws, 1, promote_ids, kmax);
  *nr = ng_rc_ws_topk(ws, 0, relax_ids, kmax);
}

void ng_rc_ws_sparse_tick(NgRcWsCtx *ws, const float origin[3], const float size[3],
                          const float eye[3]) {
  // agent: composer-2.5 | 2026-08-11 | B66 always-cover collapse tick | c46cdb
  if (!ws || !origin || !size || !eye) {
    return;
  }
  if (!ng_rc_ws_sparse_alloc(ws)) {
    return;
  }

  uint32_t fp = 2166136261u;
  fp = ng_rc_ws_hash_u32(fp, (uint32_t)(eye[0] * 32.0f));
  fp = ng_rc_ws_hash_u32(fp, (uint32_t)(eye[1] * 32.0f));
  fp = ng_rc_ws_hash_u32(fp, (uint32_t)(eye[2] * 32.0f));
  fp = ng_rc_ws_hash_u32(fp, (uint32_t)(ws->forward[0] * 32.0f));
  fp = ng_rc_ws_hash_u32(fp, (uint32_t)(ws->forward[1] * 32.0f));
  fp = ng_rc_ws_hash_u32(fp, (uint32_t)(ws->forward[2] * 32.0f));
  fp = ng_rc_ws_hash_u32(fp, (uint32_t)(origin[0] * 4.0f));
  fp = ng_rc_ws_hash_u32(fp, (uint32_t)(origin[1] * 4.0f));
  fp = ng_rc_ws_hash_u32(fp, (uint32_t)(origin[2] * 4.0f));
  fp = ng_rc_ws_hash_u32(fp, (uint32_t)(size[0] * 4.0f));
  fp = ng_rc_ws_hash_u32(fp, (uint32_t)ws->prim_count);
  fp = ng_rc_ws_hash_u32(fp, ng_rc_ws_scene_hash());

  ws->eye[0] = eye[0];
  ws->eye[1] = eye[1];
  ws->eye[2] = eye[2];
  ws->origin[0] = origin[0];
  ws->origin[1] = origin[1];
  ws->origin[2] = origin[2];
  ws->size[0] = size[0];
  ws->size[1] = size[1];
  ws->size[2] = size[2];

  s_b66_dbg_frame++;
  s_b66_splits = 0;
  s_b66_relaxes = 0;
  s_b66_inserts = 0;

  if (fp == ws->tick_fp) {
    if ((s_b66_dbg_frame % 60u) == 0u) {
      const int used = ws->slot_cap - ng_rc_ws_slot_count_free(ws);
      TraceLog(LOG_INFO, "rc-ws B66 idle used=%d free=%d/%d dirty=%d", used,
               ng_rc_ws_slot_count_free(ws), ws->slot_cap, ng_rc_ws_dirty_count(ws));
    }
    return;
  }

  int ops = 0;
  int changed = 0;

  /* Cover first — clip must stay fully tiled; then depth-only rebalance. */
  if (ng_rc_ws_cover_clip(ws, eye, &ops)) {
    changed = 1;
  }
  if (ng_rc_ws_rebalance(ws, eye, &ops)) {
    changed = 1;
  }

  ws->tick_fp = fp;
  if (changed) {
    ng_rc_ws_meta_upload_all(ws);
  }

  if ((s_b66_dbg_frame % 60u) == 0u) {
    const int used = ws->slot_cap - ng_rc_ws_slot_count_free(ws);
    int holes = 0;
    int lodmin = 99;
    int lodmax = -1;
    int in_view = 0;
    for (int i = 0; i < ws->slot_cap; i++) {
      if (!ws->slots[i].used) {
        continue;
      }
      const int L = (int)ws->slots[i].lod;
      if (L < lodmin) {
        lodmin = L;
      }
      if (L > lodmax) {
        lodmax = L;
      }
      if (ng_rc_ws_leaf_view(ws, i, eye) == 1) {
        in_view++;
      }
    }
    {
      const int clod = ng_rc_ws_cover_lod(ws);
      const float cell = ng_rc_ws_cell_size(clod);
      const int32_t ix0 = (int32_t)floorf(ws->origin[0] / cell);
      const int32_t iy0 = (int32_t)floorf(ws->origin[1] / cell);
      const int32_t iz0 = (int32_t)floorf(ws->origin[2] / cell);
      const int32_t ix1 = (int32_t)floorf((ws->origin[0] + ws->size[0]) / cell);
      const int32_t iy1 = (int32_t)floorf((ws->origin[1] + ws->size[1]) / cell);
      const int32_t iz1 = (int32_t)floorf((ws->origin[2] + ws->size[2]) / cell);
      for (int32_t iz = iz0; iz <= iz1; iz++) {
        for (int32_t iy = iy0; iy <= iy1; iy++) {
          for (int32_t ix = ix0; ix <= ix1; ix++) {
            const float px = ((float)ix + 0.5f) * cell;
            const float py = ((float)iy + 0.5f) * cell;
            const float pz = ((float)iz + 0.5f) * cell;
            if (ng_rc_ws_find_covering_scan(ws, px, py, pz) < 0) {
              holes++;
            }
          }
        }
      }
    }
    int np_dbg = 0;
    int nc_dbg = 0;
    {
      int pids[NG_RC_WS_PRIO_K];
      int cids[NG_RC_WS_PRIO_K];
      ng_rc_ws_score_prio(ws, eye, 0);
      np_dbg = ng_rc_ws_topk(ws, 1, pids, NG_RC_WS_PRIO_K);
      nc_dbg = ng_rc_ws_topk(ws, 0, cids, NG_RC_WS_PRIO_K);
    }
    TraceLog(LOG_INFO,
             "rc-ws B66 dem used=%d free=%d/%d splits=%u collapse=%u ins=%u dirty=%d ops=%d "
             "holes=%d lod=%d..%d inview=%d clod=%d np=%d nc=%d",
             used, ng_rc_ws_slot_count_free(ws), ws->slot_cap, s_b66_splits, s_b66_relaxes,
             s_b66_inserts, ng_rc_ws_dirty_count(ws), ops, holes, lodmin, lodmax, in_view,
             ng_rc_ws_cover_lod(ws), np_dbg, nc_dbg);
  }
}

/**
 * Foundation: world-aligned octree cover of culled surfaces → fair split;
 * keep only octants on SDF shells; upload hash/meta.
 */

/** Inverse-rotate v by unit quat (xyz + w) — matches rc_ws_debug.fs. */
static void ng_rc_ws_quat_inv_rotate(const float q[4], const float v[3], float o[3]) {
  const float qv[3] = {-q[0], -q[1], -q[2]};
  const float qw = q[3];
  const float t[3] = {2.0f * (qv[1] * v[2] - qv[2] * v[1]),
                      2.0f * (qv[2] * v[0] - qv[0] * v[2]),
                      2.0f * (qv[0] * v[1] - qv[1] * v[0])};
  const float c[3] = {qv[1] * t[2] - qv[2] * t[1], qv[2] * t[0] - qv[0] * t[2],
                      qv[0] * t[1] - qv[1] * t[0]};
  o[0] = v[0] + qw * t[0] + c[0];
  o[1] = v[1] + qw * t[1] + c[1];
  o[2] = v[2] + qw * t[2] + c[2];
}

/** Analytic SDF at world p (box/sphere). */
static float ng_rc_ws_prim_sdf(const NgRcWsPrim *p, const float w[3]) {
  const float d[3] = {w[0] - p->center[0], w[1] - p->center[1], w[2] - p->center[2]};
  float pl[3];
  ng_rc_ws_quat_inv_rotate(p->quat, d, pl);
  if (p->type == 1) {
    return sqrtf(pl[0] * pl[0] + pl[1] * pl[1] + pl[2] * pl[2]) - p->half[0];
  }
  const float qx = fabsf(pl[0]) - p->half[0];
  const float qy = fabsf(pl[1]) - p->half[1];
  const float qz = fabsf(pl[2]) - p->half[2];
  const float ox = fmaxf(qx, 0.0f);
  const float oy = fmaxf(qy, 0.0f);
  const float oz = fmaxf(qz, 0.0f);
  const float outside = sqrtf(ox * ox + oy * oy + oz * oz);
  const float inside = fminf(fmaxf(qx, fmaxf(qy, qz)), 0.0f);
  return outside + inside;
}

/**
 * True if cell crosses a prim shell (not pure air / pure interior).
 * Samples corners + center + face centers; keep on sign-change or |sdf| band.
 */
static int ng_rc_ws_cell_hits_prim_shell(const NgRcWsPrim *p, int lod, int32_t ix, int32_t iy,
                                        int32_t iz) {
  // agent: composer-2.5 | 2026-08-11 | SDF shell keep surface cells only | a394a2
  const float cell = ng_rc_ws_cell_size(lod);
  const float x0 = (float)ix * cell;
  const float y0 = (float)iy * cell;
  const float z0 = (float)iz * cell;
  const float h = 0.5f * cell;
  const float band = h * 1.7320508f; /* half space-diagonal */
  float mind = 1e30f;
  float maxd = -1e30f;
  float minabs = 1e30f;
  /* 8 corners, center, 6 face centers */
  const float samples[15][3] = {
      {x0, y0, z0},
      {x0 + cell, y0, z0},
      {x0, y0 + cell, z0},
      {x0 + cell, y0 + cell, z0},
      {x0, y0, z0 + cell},
      {x0 + cell, y0, z0 + cell},
      {x0, y0 + cell, z0 + cell},
      {x0 + cell, y0 + cell, z0 + cell},
      {x0 + h, y0 + h, z0 + h},
      {x0 + h, y0 + h, z0},
      {x0 + h, y0 + h, z0 + cell},
      {x0 + h, y0, z0 + h},
      {x0 + h, y0 + cell, z0 + h},
      {x0, y0 + h, z0 + h},
      {x0 + cell, y0 + h, z0 + h},
  };
  for (int s = 0; s < 15; s++) {
    const float d = ng_rc_ws_prim_sdf(p, samples[s]);
    if (d < mind) {
      mind = d;
    }
    if (d > maxd) {
      maxd = d;
    }
    const float a = fabsf(d);
    if (a < minabs) {
      minabs = a;
    }
  }
  if (mind < 0.0f && maxd > 0.0f) {
    return 1;
  }
  if (minabs <= band) {
    return 1;
  }
  return 0;
}

/** Cell hits any visible prim SDF shell (AABB broadphase then shell). */
static int ng_rc_ws_cell_overlaps_vis(const NgRcWsCtx *ws, int lod, int32_t ix, int32_t iy,
                                     int32_t iz, const int *vis_prims, int vis_n) {
  // agent: composer-2.5 | 2026-08-11 | SDF shell keep surface cells only | a394a2
  const float cell = ng_rc_ws_cell_size(lod);
  const float cmin[3] = {(float)ix * cell, (float)iy * cell, (float)iz * cell};
  const float cmax[3] = {cmin[0] + cell, cmin[1] + cell, cmin[2] + cell};
  for (int i = 0; i < vis_n; i++) {
    const int pi = vis_prims[i];
    if (pi < 0 || pi >= ws->prim_count) {
      continue;
    }
    float pmin[3], pmax[3];
    ng_rc_ws_prim_aabb(&ws->prims[pi], pmin, pmax);
    if (pmin[0] > cmax[0] || pmax[0] < cmin[0] || pmin[1] > cmax[1] || pmax[1] < cmin[1] ||
        pmin[2] > cmax[2] || pmax[2] < cmin[2]) {
      continue;
    }
    if (ng_rc_ws_cell_hits_prim_shell(&ws->prims[pi], lod, ix, iy, iz)) {
      return 1;
    }
  }
  return 0;
}

/** Count used slots. */
static int ng_rc_ws_slot_used_count(const NgRcWsCtx *ws) {
  int n = 0;
  for (int i = 0; i < ws->slot_cap; i++) {
    if (ws->slots[i].used) {
      n++;
    }
  }
  return n;
}

/**
 * Owner of a leaf: overlapping vis prim whose center is closest to cell center.
 * @return prim index or -1.
 */
static int ng_rc_ws_leaf_owner(const NgRcWsCtx *ws, int si, const int *vis_prims, int vis_n) {
  if (si < 0 || !ws->slots[si].used || !vis_prims || vis_n <= 0) {
    return -1;
  }
  const int lod = (int)ws->slots[si].lod;
  const float cell = ng_rc_ws_cell_size(lod);
  const float cx = ((float)ws->slots[si].ix + 0.5f) * cell;
  const float cy = ((float)ws->slots[si].iy + 0.5f) * cell;
  const float cz = ((float)ws->slots[si].iz + 0.5f) * cell;
  const float cmin[3] = {(float)ws->slots[si].ix * cell, (float)ws->slots[si].iy * cell,
                         (float)ws->slots[si].iz * cell};
  const float cmax[3] = {cmin[0] + cell, cmin[1] + cell, cmin[2] + cell};
  int best = -1;
  float best_d2 = 1e30f;
  for (int i = 0; i < vis_n; i++) {
    const int pi = vis_prims[i];
    if (pi < 0 || pi >= ws->prim_count) {
      continue;
    }
    float pmin[3], pmax[3];
    ng_rc_ws_prim_aabb(&ws->prims[pi], pmin, pmax);
    if (pmin[0] > cmax[0] || pmax[0] < cmin[0] || pmin[1] > cmax[1] || pmax[1] < cmin[1] ||
        pmin[2] > cmax[2] || pmax[2] < cmin[2]) {
      continue;
    }
    const float dx = ws->prims[pi].center[0] - cx;
    const float dy = ws->prims[pi].center[1] - cy;
    const float dz = ws->prims[pi].center[2] - cz;
    const float d2 = dx * dx + dy * dy + dz * dz;
    if (d2 < best_d2) {
      best_d2 = d2;
      best = pi;
    }
  }
  return best;
}

/** Fill count[prim] = number of used leaves owned by that prim. */
static void ng_rc_ws_count_tiles_per_mesh(const NgRcWsCtx *ws, const int *vis_prims, int vis_n,
                                         int *count) {
  memset(count, 0, (size_t)NG_RC_WS_PRIM_MAX * sizeof(int));
  for (int i = 0; i < ws->slot_cap; i++) {
    if (!ws->slots[i].used) {
      continue;
    }
    const int own = ng_rc_ws_leaf_owner(ws, i, vis_prims, vis_n);
    if (own >= 0 && own < NG_RC_WS_PRIM_MAX) {
      count[own]++;
    }
  }
}

/**
 * Octree split: replace leaf with only children that overlap culled AABBs.
 * @return 1 if structure changed.
 */
static int ng_rc_ws_apply_split_surface(NgRcWsCtx *ws, int i, const int *vis_prims, int vis_n) {
  // agent: composer-2.5 | 2026-08-11 | octree cover split surface tick | 6ba63e
  if (i < 0 || !ws->slots[i].used || ws->slots[i].lod == 0) {
    return 0;
  }
  const int child_lod = (int)ws->slots[i].lod - 1;
  const int32_t bx = ws->slots[i].ix * 2;
  const int32_t by = ws->slots[i].iy * 2;
  const int32_t bz = ws->slots[i].iz * 2;
  int keep[8];
  int nk = 0;
  for (int c = 0; c < 8; c++) {
    const int32_t cx = bx + (c & 1);
    const int32_t cy = by + ((c >> 1) & 1);
    const int32_t cz = bz + ((c >> 2) & 1);
    if (ng_rc_ws_cell_overlaps_vis(ws, child_lod, cx, cy, cz, vis_prims, vis_n)) {
      keep[nk++] = c;
    }
  }
  if (nk == 0) {
    ws->slots[i].used = 0;
    ws->slots[i].dirty = 0;
    return 1;
  }
  /* Parent will free; need nk free after that. */
  if (ng_rc_ws_slot_count_free(ws) + 1 < nk) {
    return 0;
  }
  const NgRcWsSlot saved = ws->slots[i];
  ws->slots[i].used = 0;
  ws->slots[i].dirty = 0;
  int kids[8];
  for (int k = 0; k < nk; k++) {
    kids[k] = ng_rc_ws_slot_alloc(ws);
    if (kids[k] < 0) {
      for (int j = 0; j < k; j++) {
        ws->slots[kids[j]].used = 0;
        ws->slots[kids[j]].dirty = 0;
      }
      ws->slots[i] = saved;
      return 0;
    }
    ws->slots[kids[k]].used = 1;
  }
  for (int k = 0; k < nk; k++) {
    const int c = keep[k];
    const int si = kids[k];
    ws->slots[si].lod = (uint8_t)child_lod;
    ws->slots[si].ix = bx + (c & 1);
    ws->slots[si].iy = by + ((c >> 1) & 1);
    ws->slots[si].iz = bz + ((c >> 2) & 1);
    ws->slots[si].dirty = 0;
    ws->slots[si].pad = 0;
  }
  return 1;
}

/** Stamp world-aligned surface cells over one prim AABB at lod; stop at budget. */
static void ng_rc_ws_surface_cover_prim(NgRcWsCtx *ws, int pi, int lod, int budget) {
  // agent: composer-2.5 | 2026-08-11 | SDF shell keep surface cells only | a394a2
  if (pi < 0 || pi >= ws->prim_count) {
    return;
  }
  float pmin[3], pmax[3];
  ng_rc_ws_prim_aabb(&ws->prims[pi], pmin, pmax);
  const float cell = ng_rc_ws_cell_size(lod);
  if (cell < 1e-5f) {
    return;
  }
  const int32_t ix0 = (int32_t)floorf(pmin[0] / cell);
  const int32_t iy0 = (int32_t)floorf(pmin[1] / cell);
  const int32_t iz0 = (int32_t)floorf(pmin[2] / cell);
  const int32_t ix1 = (int32_t)floorf(pmax[0] / cell);
  const int32_t iy1 = (int32_t)floorf(pmax[1] / cell);
  const int32_t iz1 = (int32_t)floorf(pmax[2] / cell);
  for (int32_t iz = iz0; iz <= iz1; iz++) {
    for (int32_t iy = iy0; iy <= iy1; iy++) {
      for (int32_t ix = ix0; ix <= ix1; ix++) {
        if (ng_rc_ws_slot_used_count(ws) >= budget || ng_rc_ws_slot_count_free(ws) < 1) {
          return;
        }
        if (!ng_rc_ws_cell_hits_prim_shell(&ws->prims[pi], lod, ix, iy, iz)) {
          continue;
        }
        (void)ng_rc_ws_apply_insert_cell(ws, (uint8_t)lod, ix, iy, iz);
      }
    }
  }
}

/** Approximate surface area of analytic prim (AABB shell). */
static float ng_rc_ws_prim_surf_area(const NgRcWsPrim *p) {
  float pmin[3], pmax[3];
  ng_rc_ws_prim_aabb(p, pmin, pmax);
  const float lx = fmaxf(pmax[0] - pmin[0], 1e-4f);
  const float ly = fmaxf(pmax[1] - pmin[1], 1e-4f);
  const float lz = fmaxf(pmax[2] - pmin[2], 1e-4f);
  return 2.0f * (lx * ly + ly * lz + lz * lx);
}

/**
 * World-aligned octree: coarse cover every culled prim → gen-complete waves;
 * empty octants discarded; fill toward 50% pool.
 */
void ng_rc_ws_surface_tick(NgRcWsCtx *ws, const int *vis_prims, int vis_n) {
  // agent: composer-2.5 | 2026-08-11 | restore cover then gen wave | 8e1bf4
  if (!ws || !ws->ready) {
    return;
  }
  if (!ng_rc_ws_sparse_alloc(ws)) {
    return;
  }
  for (int i = 0; i < ws->slot_cap; i++) {
    ws->slots[i].used = 0;
    ws->slots[i].dirty = 0;
    ws->slots[i].pad = 0;
  }
  ws->slot_count = 0;
  if (!vis_prims || vis_n <= 0) {
    ng_rc_ws_meta_upload_all(ws);
    TraceLog(LOG_INFO, "rc-ws surface cells=0 budget=0/%d vis=0", ws->slot_cap);
    return;
  }

  const int budget = ws->slot_cap / 2; /* hard 50% — e.g. 256/512 at default quality */
  // agent: composer-2.5 | 2026-08-11 | enforce surface budget half pool | de62a7
  /* Gen 0: stamp coarse world cells over each culled prim AABB (full surface cover). */
  const int cover_lod = NG_RC_WS_LOD_MAX - 2; /* cell = 25.6m — same as pre-wave path */
  for (int i = 0; i < vis_n; i++) {
    if (ng_rc_ws_slot_used_count(ws) >= budget) {
      break;
    }
    ng_rc_ws_surface_cover_prim(ws, vis_prims[i], cover_lod, budget);
  }

  /* Generation waves: finish every snapped leaf; lod-locked (no mid-wave drill). */
  int gens = 0;
  for (;;) {
    const int used0 = ng_rc_ws_slot_used_count(ws);
    if (used0 >= budget) {
      break;
    }
    int snap_i[NG_RC_WS_SLOT_MAX];
    uint8_t snap_lod[NG_RC_WS_SLOT_MAX];
    int ns = 0;
    for (int i = 0; i < ws->slot_cap && ns < NG_RC_WS_SLOT_MAX; i++) {
      if (!ws->slots[i].used || ws->slots[i].lod == 0) {
        continue;
      }
      snap_i[ns] = i;
      snap_lod[ns] = ws->slots[i].lod;
      ns++;
    }
    if (ns <= 0) {
      break;
    }

    int split_ok = 0;
    for (int s = 0; s < ns; s++) {
      const int si = snap_i[s];
      if (!ws->slots[si].used || ws->slots[si].lod != snap_lod[s]) {
        continue;
      }
      if (!ng_rc_ws_apply_split_surface(ws, si, vis_prims, vis_n)) {
        continue;
      }
      split_ok = 1;
    }
    if (!split_ok) {
      break;
    }
    gens++;
  }

  for (int i = 0; i < ws->slot_cap; i++) {
    if (!ws->slots[i].used) {
      continue;
    }
    if (!ng_rc_ws_cell_overlaps_vis(ws, (int)ws->slots[i].lod, ws->slots[i].ix, ws->slots[i].iy,
                                   ws->slots[i].iz, vis_prims, vis_n)) {
      ws->slots[i].used = 0;
      ws->slots[i].dirty = 0;
    }
  }

  ng_rc_ws_meta_upload_all(ws);
  {
    const int used = ng_rc_ws_slot_used_count(ws);
    int lodmin = 99;
    int lodmax = -1;
    for (int i = 0; i < ws->slot_cap; i++) {
      if (!ws->slots[i].used) {
        continue;
      }
      const int L = (int)ws->slots[i].lod;
      if (L < lodmin) {
        lodmin = L;
      }
      if (L > lodmax) {
        lodmax = L;
      }
    }
    if (lodmax < 0) {
      lodmin = 0;
      lodmax = 0;
    }
    static int s_log_used = -1;
    static int s_log_gens = -1;
    static int s_log_vis = -1;
    static int s_log_budget = -1;
    if (used != s_log_used || gens != s_log_gens || vis_n != s_log_vis || budget != s_log_budget) {
      s_log_used = used;
      s_log_gens = gens;
      s_log_vis = vis_n;
      s_log_budget = budget;
      TraceLog(LOG_INFO,
               "rc-ws surface cells=%d budget=%d/%d gens=%d lod=%d..%d vis=%d cover_lod=%d", used,
               budget, ws->slot_cap, gens, lodmin, lodmax, vis_n, cover_lod);
    }
  }
}

void ng_rc_ws_probe_clear(NgRcWsCtx *ws) {
  // agent: composer-2.5 | 2026-08-12 | GPU probe tick API | af2c49
  if (!ws || !ws->ready || !ng_rc_ws_sparse_alloc(ws)) {
    return;
  }
  for (int i = 0; i < ws->slot_cap; i++) {
    ws->slots[i].used = 0;
    ws->slots[i].dirty = 0;
    ws->slots[i].pad = 0;
  }
  ws->slot_count = 0;
}

int ng_rc_ws_probe_insert_cell(NgRcWsCtx *ws, uint8_t lod, int32_t ix, int32_t iy, int32_t iz) {
  if (!ws) {
    return -1;
  }
  return ng_rc_ws_apply_insert_cell(ws, lod, ix, iy, iz) ? 1 : -1;
}

int ng_rc_ws_probe_used(const NgRcWsCtx *ws) {
  return ws ? ng_rc_ws_slot_used_count(ws) : 0;
}

void ng_rc_ws_probe_upload(NgRcWsCtx *ws) {
  if (!ws || !ws->ready) {
    return;
  }
  ng_rc_ws_meta_upload_all(ws);
}

int ng_rc_ws_probe_apply_split_mask(NgRcWsCtx *ws, int si, unsigned keep_mask) {
  // agent: composer-2.5 | 2026-08-12 | GPU probe tick API | af2c49
  if (!ws || si < 0 || !ws->slots[si].used || ws->slots[si].lod == 0) {
    return 0;
  }
  const int child_lod = (int)ws->slots[si].lod - 1;
  const int32_t bx = ws->slots[si].ix * 2;
  const int32_t by = ws->slots[si].iy * 2;
  const int32_t bz = ws->slots[si].iz * 2;
  int keep[8];
  int nk = 0;
  for (int c = 0; c < 8; c++) {
    if (keep_mask & (1u << c)) {
      keep[nk++] = c;
    }
  }
  if (nk == 0) {
    ws->slots[si].used = 0;
    ws->slots[si].dirty = 0;
    return 1;
  }
  if (ng_rc_ws_slot_count_free(ws) + 1 < nk) {
    return 0;
  }
  const NgRcWsSlot saved = ws->slots[si];
  ws->slots[si].used = 0;
  ws->slots[si].dirty = 0;
  int kids[8];
  for (int k = 0; k < nk; k++) {
    kids[k] = ng_rc_ws_slot_alloc(ws);
    if (kids[k] < 0) {
      for (int j = 0; j < k; j++) {
        ws->slots[kids[j]].used = 0;
        ws->slots[kids[j]].dirty = 0;
      }
      ws->slots[si] = saved;
      return 0;
    }
    ws->slots[kids[k]].used = 1;
  }
  for (int k = 0; k < nk; k++) {
    const int c = keep[k];
    const int kid = kids[k];
    ws->slots[kid].lod = (uint8_t)child_lod;
    ws->slots[kid].ix = bx + (c & 1);
    ws->slots[kid].iy = by + ((c >> 1) & 1);
    ws->slots[kid].iz = bz + ((c >> 2) & 1);
    ws->slots[kid].dirty = 0;
    ws->slots[kid].pad = 0;
  }
  return 1;
}

int ng_rc_ws_probe_enum_cover(const NgRcWsCtx *ws, int pi, int lod, int32_t *ix, int32_t *iy,
                              int32_t *iz, int *prim_out, int cap) {
  // agent: composer-2.5 | 2026-08-12 | GPU probe tick API | af2c49
  if (!ws || pi < 0 || pi >= ws->prim_count || !ix || !iy || !iz || !prim_out || cap <= 0) {
    return 0;
  }
  float pmin[3], pmax[3];
  ng_rc_ws_prim_aabb(&ws->prims[pi], pmin, pmax);
  const float cell = ng_rc_ws_cell_size(lod);
  if (cell < 1e-5f) {
    return 0;
  }
  const int32_t ix0 = (int32_t)floorf(pmin[0] / cell);
  const int32_t iy0 = (int32_t)floorf(pmin[1] / cell);
  const int32_t iz0 = (int32_t)floorf(pmin[2] / cell);
  const int32_t ix1 = (int32_t)floorf(pmax[0] / cell);
  const int32_t iy1 = (int32_t)floorf(pmax[1] / cell);
  const int32_t iz1 = (int32_t)floorf(pmax[2] / cell);
  int n = 0;
  for (int32_t z = iz0; z <= iz1 && n < cap; z++) {
    for (int32_t y = iy0; y <= iy1 && n < cap; y++) {
      for (int32_t x = ix0; x <= ix1 && n < cap; x++) {
        ix[n] = x;
        iy[n] = y;
        iz[n] = z;
        prim_out[n] = pi;
        n++;
      }
    }
  }
  return n;
}

int ng_rc_ws_probe_enum_children(const NgRcWsCtx *ws, int si, int32_t *ix, int32_t *iy, int32_t *iz,
                                 int *lod_out, int *parent_out, int *child_out, int cap) {
  // agent: composer-2.5 | 2026-08-12 | GPU probe tick API | af2c49
  if (!ws || si < 0 || !ws->slots[si].used || ws->slots[si].lod == 0 || cap < 8) {
    return 0;
  }
  const int child_lod = (int)ws->slots[si].lod - 1;
  const int32_t bx = ws->slots[si].ix * 2;
  const int32_t by = ws->slots[si].iy * 2;
  const int32_t bz = ws->slots[si].iz * 2;
  for (int c = 0; c < 8; c++) {
    ix[c] = bx + (c & 1);
    iy[c] = by + ((c >> 1) & 1);
    iz[c] = bz + ((c >> 2) & 1);
    lod_out[c] = child_lod;
    parent_out[c] = si;
    child_out[c] = c;
  }
  return 8;
}

int ng_rc_ws_probe_cell_keep(const NgRcWsCtx *ws, int pi, int lod, int32_t ix, int32_t iy,
                             int32_t iz) {
  // agent: composer-2.5 | 2026-08-12 | probe incremental helpers | a22078
  if (!ws || pi < 0 || pi >= ws->prim_count) {
    return 0;
  }
  return ng_rc_ws_cell_hits_prim_shell(&ws->prims[pi], lod, ix, iy, iz);
}

uint32_t ng_rc_ws_probe_view_fp(const NgRcWsCtx *ws, const float eye[3], const float forward[3]) {
  // agent: composer-2.5 | 2026-08-12 | probe incremental helpers | a22078
  uint32_t h = ws ? ws->scene_hash : 0u;
  if (!eye || !forward) {
    return h;
  }
  /* ~0.25m / ~few degrees — skip probe rebuild while view idle. */
  const int ex = (int)floorf(eye[0] * 4.0f);
  const int ey = (int)floorf(eye[1] * 4.0f);
  const int ez = (int)floorf(eye[2] * 4.0f);
  const int fx = (int)floorf(forward[0] * 16.0f);
  const int fy = (int)floorf(forward[1] * 16.0f);
  const int fz = (int)floorf(forward[2] * 16.0f);
  h ^= (uint32_t)(ex * 73856093) ^ (uint32_t)(ey * 19349663) ^ (uint32_t)(ez * 83492791);
  h ^= (uint32_t)(fx * 2654435761u) ^ (uint32_t)(fy * 2246822519u) ^ (uint32_t)(fz * 3266489917u);
  if (ws) {
    h ^= (uint32_t)ws->slot_cap * 0x9e3779b9u;
    h ^= (uint32_t)ws->prim_count * 0x85ebca6bu;
  }
  return h;
}

void ng_rc_ws_probe_evict_unvis(NgRcWsCtx *ws, const int *vis_prims, int vis_n) {
  // agent: composer-2.5 | 2026-08-12 | probe incremental helpers | a22078
  if (!ws || !vis_prims || vis_n <= 0) {
    return;
  }
  for (int i = 0; i < ws->slot_cap; i++) {
    if (!ws->slots[i].used) {
      continue;
    }
    if (!ng_rc_ws_cell_overlaps_vis(ws, (int)ws->slots[i].lod, ws->slots[i].ix, ws->slots[i].iy,
                                   ws->slots[i].iz, vis_prims, vis_n)) {
      ws->slots[i].used = 0;
      ws->slots[i].dirty = 0;
    }
  }
}

// agent: grok-4.6 | 2026-08-12 | lazy probe CPU oracle tick | 2dd6ca
static uint32_t ng_rc_ws_stoch_stable(int si) {
  // agent: grok-4.6 | 2026-08-12 | persist cover-first relax | 052e50
  uint32_t h = (uint32_t)si * 2654435761u;
  h ^= h >> 16;
  return h;
}

int ng_rc_ws_probe_octant_occupied(const NgRcWsCtx *ws, int lod, int32_t ix, int32_t iy,
                                   int32_t iz) {
  if (!ws || lod < 0) {
    return 0;
  }
  for (int i = 0; i < ws->slot_cap; i++) {
    if (!ws->slots[i].used) {
      continue;
    }
    const int sl = (int)ws->slots[i].lod;
    const int32_t scx = ws->slots[i].ix;
    const int32_t scy = ws->slots[i].iy;
    const int32_t scz = ws->slots[i].iz;
    if (sl == lod) {
      if (scx == ix && scy == iy && scz == iz) {
        return 1;
      }
    } else if (sl < lod) {
      const int d = lod - sl;
      const int32_t den = 1 << d;
      if (scx / den == ix && scy / den == iy && scz / den == iz) {
        return 1;
      }
    } else {
      const int d = sl - lod;
      const int32_t den = 1 << d;
      if (ix / den == scx && iy / den == scy && iz / den == scz) {
        return 1;
      }
    }
  }
  return 0;
}

int ng_rc_ws_probe_cpu_begin(NgRcWsCtx *ws, int slot_cap) {
  // agent: grok-4.6 | 2026-08-12 | lazy probe CPU oracle tick | 2dd6ca
  if (!ws) {
    return 0;
  }
  ng_rc_ws_init(ws);
  if (slot_cap < 8) {
    slot_cap = 8;
  }
  if (slot_cap > NG_RC_WS_SLOT_MAX) {
    slot_cap = NG_RC_WS_SLOT_MAX;
  }
  ws->slot_cap = slot_cap;
  ws->probe_n = slot_cap;
  ws->ready = true;
  if (!ng_rc_ws_sparse_alloc(ws)) {
    ws->ready = false;
    return 0;
  }
  for (int i = 0; i < ws->slot_cap; i++) {
    ws->slots[i].used = 0;
    ws->slots[i].dirty = 0;
    ws->slots[i].pad = 0;
  }
  ws->slot_count = 0;
  return 1;
}

int ng_rc_ws_probe_lod_min(const NgRcWsCtx *ws) {
  int m = 99;
  if (!ws) {
    return m;
  }
  for (int i = 0; i < ws->slot_cap; i++) {
    if (ws->slots[i].used && (int)ws->slots[i].lod < m) {
      m = (int)ws->slots[i].lod;
    }
  }
  return m;
}

int ng_rc_ws_probe_lod_max(const NgRcWsCtx *ws) {
  int m = -1;
  if (!ws) {
    return m;
  }
  for (int i = 0; i < ws->slot_cap; i++) {
    if (ws->slots[i].used && (int)ws->slots[i].lod > m) {
      m = (int)ws->slots[i].lod;
    }
  }
  return m;
}

void ng_rc_ws_probe_hist_text(const NgRcWsCtx *ws, char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  out[0] = '\0';
  if (!ws) {
    return;
  }
  int hist[NG_RC_WS_LOD_SOFT_MAX + 1];
  memset(hist, 0, sizeof(hist));
  const int used = ng_rc_ws_slot_used_count(ws);
  const int budget = ws->slot_cap / 2;
  for (int i = 0; i < ws->slot_cap; i++) {
    if (!ws->slots[i].used) {
      continue;
    }
    int L = (int)ws->slots[i].lod;
    if (L < 0) {
      L = 0;
    } else if (L > NG_RC_WS_LOD_SOFT_MAX) {
      L = NG_RC_WS_LOD_SOFT_MAX;
    }
    hist[L]++;
  }
  size_t n = (size_t)snprintf(out, cap, "used=%d free=%d headroom=%d budget=%d/%d min=%d max=%d",
                              used, ws->slot_cap - used, budget - used > 0 ? budget - used : 0,
                              budget, ws->slot_cap, ng_rc_ws_probe_lod_min(ws),
                              ng_rc_ws_probe_lod_max(ws));
  for (int L = NG_RC_WS_LOD_SOFT_MAX; L >= 0 && n + 16 < cap; L--) {
    if (hist[L] <= 0) {
      continue;
    }
    n += (size_t)snprintf(out + n, cap - n, " L%d=%d", L, hist[L]);
  }
}

int ng_rc_ws_probe_slots_on_prim(const NgRcWsCtx *ws, int pi) {
  // agent: grok-4.6 | 2026-08-12 | cover unmet helper API | 5f4345
  if (!ws || pi < 0 || pi >= ws->prim_count) {
    return 0;
  }
  int n = 0;
  for (int i = 0; i < ws->slot_cap; i++) {
    if (!ws->slots[i].used) {
      continue;
    }
    if (ng_rc_ws_cell_hits_prim_shell(&ws->prims[pi], (int)ws->slots[i].lod, ws->slots[i].ix,
                                      ws->slots[i].iy, ws->slots[i].iz)) {
      n++;
    }
  }
  return n;
}

int ng_rc_ws_probe_prim_cover_ok(const NgRcWsCtx *ws, int pi) {
  return ng_rc_ws_probe_slots_on_prim(ws, pi) > 0;
}

int ng_rc_ws_probe_cover_unmet(const NgRcWsCtx *ws, const int *vis_prims, int vis_n) {
  // agent: grok-4.6 | 2026-08-12 | cover unmet helper API | 5f4345
  if (!ws || !vis_prims || vis_n <= 0) {
    return 0;
  }
  const int cover_lod = NG_RC_WS_LOD_MAX - 2;
  const float cell = ng_rc_ws_cell_size(cover_lod);
  if (cell < 1e-5f) {
    return 0;
  }
  for (int vi = 0; vi < vis_n; vi++) {
    const int pi = vis_prims[vi];
    if (pi < 0 || pi >= ws->prim_count) {
      continue;
    }
    float pmin[3], pmax[3];
    ng_rc_ws_prim_aabb(&ws->prims[pi], pmin, pmax);
    const int32_t ix0 = (int32_t)floorf(pmin[0] / cell);
    const int32_t iy0 = (int32_t)floorf(pmin[1] / cell);
    const int32_t iz0 = (int32_t)floorf(pmin[2] / cell);
    const int32_t ix1 = (int32_t)floorf(pmax[0] / cell);
    const int32_t iy1 = (int32_t)floorf(pmax[1] / cell);
    const int32_t iz1 = (int32_t)floorf(pmax[2] / cell);
    for (int32_t iz = iz0; iz <= iz1; iz++) {
      for (int32_t iy = iy0; iy <= iy1; iy++) {
        for (int32_t ix = ix0; ix <= ix1; ix++) {
          if (!ng_rc_ws_cell_hits_prim_shell(&ws->prims[pi], cover_lod, ix, iy, iz)) {
            continue;
          }
          if (!ng_rc_ws_probe_octant_occupied(ws, cover_lod, ix, iy, iz)) {
            return 1;
          }
        }
      }
    }
  }
  return 0;
}

uint64_t ng_rc_ws_probe_key_fp(const NgRcWsCtx *ws) {
  // agent: grok-4.6 | 2026-08-12 | persist cover-first relax | 052e50
  uint64_t h = 1469598103934665603ull;
  if (!ws) {
    return h;
  }
  for (int i = 0; i < ws->slot_cap; i++) {
    if (!ws->slots[i].used) {
      continue;
    }
    uint64_t k = ((uint64_t)ws->slots[i].lod << 48) ^ ((uint64_t)(uint32_t)ws->slots[i].ix << 32) ^
                 ((uint64_t)(uint32_t)ws->slots[i].iy << 16) ^ (uint64_t)(uint32_t)ws->slots[i].iz;
    h ^= k;
    h *= 1099511628211ull;
  }
  return h;
}

/** Insert cover cell if octant free and under budget. */
static int ng_rc_ws_lazy_insert(NgRcWsCtx *ws, uint8_t lod, int32_t ix, int32_t iy, int32_t iz,
                                int budget) {
  if (ng_rc_ws_slot_used_count(ws) >= budget) {
    return 0;
  }
  if (ng_rc_ws_probe_octant_occupied(ws, (int)lod, ix, iy, iz)) {
    return 0;
  }
  if (ng_rc_ws_slot_count_free(ws) < 1) {
    return 0;
  }
  const int si = ng_rc_ws_slot_alloc(ws);
  if (si < 0) {
    return 0;
  }
  ws->slots[si].lod = lod;
  ws->slots[si].ix = ix;
  ws->slots[si].iy = iy;
  ws->slots[si].iz = iz;
  ws->slots[si].used = 1;
  ws->slots[si].dirty = 0;
  ws->slots[si].pad = 0;
  return 1;
}

/** Free up to quota finest (lod<=2) slots by stable steal score. */
static int ng_rc_ws_steal_finest(NgRcWsCtx *ws, int quota) {
  // agent: grok-4.6 | 2026-08-12 | persist cover-first relax | 052e50
  if (!ws || quota <= 0) {
    return 0;
  }
  int freed = 0;
  for (int pass = 0; pass < quota; pass++) {
    int best = -1;
    uint32_t best_sc = 0;
    for (int i = 0; i < ws->slot_cap; i++) {
      if (!ws->slots[i].used || (int)ws->slots[i].lod > 2) {
        continue;
      }
      const int lod = (int)ws->slots[i].lod;
      const int fine = 3 - lod;
      uint32_t sc = (uint32_t)fine * 100000u + (ng_rc_ws_stoch_stable(i) % 100000u);
      if (best < 0 || sc > best_sc || (sc == best_sc && i < best)) {
        best = i;
        best_sc = sc;
      }
    }
    if (best < 0) {
      break;
    }
    ws->slots[best].used = 0;
    ws->slots[best].dirty = 0;
    freed++;
  }
  return freed;
}

/** Collapse up to quota fine leaves into parents (relax when stuck). */
static int ng_rc_ws_relax_collapse(NgRcWsCtx *ws, int quota) {
  // agent: grok-4.6 | 2026-08-12 | persist cover-first relax | 052e50
  if (!ws || quota <= 0) {
    return 0;
  }
  int n = 0;
  uint8_t tried[NG_RC_WS_SLOT_MAX];
  memset(tried, 0, (size_t)ws->slot_cap);
  while (n < quota) {
    int best = -1;
    int best_lod = 99;
    for (int i = 0; i < ws->slot_cap; i++) {
      if (!ws->slots[i].used || tried[i]) {
        continue;
      }
      const int lod = (int)ws->slots[i].lod;
      if (lod >= NG_RC_WS_LOD_MAX - 1) {
        continue;
      }
      if (best < 0 || lod < best_lod || (lod == best_lod && i < best)) {
        best = i;
        best_lod = lod;
      }
    }
    if (best < 0) {
      break;
    }
    tried[best] = 1;
    if (ng_rc_ws_apply_collapse(ws, best)) {
      n++;
      memset(tried, 0, (size_t)ws->slot_cap);
    }
  }
  return n;
}

/** One cover batch: insert up to cover_k unmet cells. */
static int ng_rc_ws_lazy_cover_batch(NgRcWsCtx *ws, const int *vis_prims, int vis_n, int cover_k,
                                    int budget, int cover_lod) {
  int inserted = 0;
  for (int vi = 0; vi < vis_n && inserted < cover_k; vi++) {
    const int pi = vis_prims[vi];
    if (pi < 0 || pi >= ws->prim_count) {
      continue;
    }
    float pmin[3], pmax[3];
    ng_rc_ws_prim_aabb(&ws->prims[pi], pmin, pmax);
    const float cell = ng_rc_ws_cell_size(cover_lod);
    if (cell < 1e-5f) {
      continue;
    }
    const int32_t ix0 = (int32_t)floorf(pmin[0] / cell);
    const int32_t iy0 = (int32_t)floorf(pmin[1] / cell);
    const int32_t iz0 = (int32_t)floorf(pmin[2] / cell);
    const int32_t ix1 = (int32_t)floorf(pmax[0] / cell);
    const int32_t iy1 = (int32_t)floorf(pmax[1] / cell);
    const int32_t iz1 = (int32_t)floorf(pmax[2] / cell);
    for (int32_t iz = iz0; iz <= iz1 && inserted < cover_k; iz++) {
      for (int32_t iy = iy0; iy <= iy1 && inserted < cover_k; iy++) {
        for (int32_t ix = ix0; ix <= ix1 && inserted < cover_k; ix++) {
          if (ng_rc_ws_slot_used_count(ws) >= budget) {
            return inserted;
          }
          if (!ng_rc_ws_cell_hits_prim_shell(&ws->prims[pi], cover_lod, ix, iy, iz)) {
            continue;
          }
          if (ng_rc_ws_lazy_insert(ws, (uint8_t)cover_lod, ix, iy, iz, budget)) {
            inserted++;
          }
        }
      }
    }
  }
  return inserted;
}

void ng_rc_ws_probe_lazy_tick(NgRcWsCtx *ws, const int *vis_prims, int vis_n, uint32_t frame,
                              int cover_k, int split_k) {
  // agent: grok-4.6 | 2026-08-12 | persist cover-first relax | 052e50
  (void)frame;
  if (!ws || !ws->ready) {
    return;
  }
  if (!ng_rc_ws_sparse_alloc(ws)) {
    return;
  }
  if (cover_k < 0) {
    cover_k = 0;
  }
  if (split_k < 0) {
    split_k = 0;
  }
  if (cover_k > 64) {
    cover_k = 64;
  }
  if (split_k > 64) {
    split_k = 64;
  }
  const int budget = ws->slot_cap / 2;
  const int cover_lod = NG_RC_WS_LOD_MAX - 2;
  const int relax_k = split_k > cover_k ? split_k : cover_k;

  /* 1) Release empty / unvis */
  if (vis_prims && vis_n > 0) {
    ng_rc_ws_probe_evict_unvis(ws, vis_prims, vis_n);
  } else {
    for (int i = 0; i < ws->slot_cap; i++) {
      ws->slots[i].used = 0;
      ws->slots[i].dirty = 0;
    }
    ws->slot_count = 0;
    return;
  }

  /* 2) Cover while headroom */
  if (cover_k > 0) {
    (void)ng_rc_ws_lazy_cover_batch(ws, vis_prims, vis_n, cover_k, budget, cover_lod);
  }

  /* 3) Relax if stuck at budget with unmet cover; cover again */
  if (relax_k > 0 && ng_rc_ws_slot_used_count(ws) >= budget &&
      ng_rc_ws_probe_cover_unmet(ws, vis_prims, vis_n)) {
    if (ng_rc_ws_relax_collapse(ws, relax_k) > 0 && cover_k > 0) {
      (void)ng_rc_ws_lazy_cover_batch(ws, vis_prims, vis_n, cover_k, budget, cover_lod);
    }
  }

  /* 4) Split when cover satisfied OR coarse band exists to refine */
  {
    const int unmet = ng_rc_ws_probe_cover_unmet(ws, vis_prims, vis_n);
    int lod_hi = ng_rc_ws_probe_lod_max(ws);
    const int allow_split = !unmet || (lod_hi >= cover_lod && lod_hi > 0);
    int headroom = budget - ng_rc_ws_slot_used_count(ws);
    int splits = 0;
    if (allow_split && lod_hi > 0 && headroom > 0 && split_k > 0) {
      for (int si = 0; si < ws->slot_cap && splits < split_k; si++) {
        if (!ws->slots[si].used || (int)ws->slots[si].lod != lod_hi) {
          continue;
        }
        if ((int)ws->slots[si].lod <= 0) {
          continue;
        }
        const int child_lod = (int)ws->slots[si].lod - 1;
        const int32_t bx = ws->slots[si].ix * 2;
        const int32_t by = ws->slots[si].iy * 2;
        const int32_t bz = ws->slots[si].iz * 2;
        int nk = 0;
        for (int c = 0; c < 8; c++) {
          if (ng_rc_ws_cell_overlaps_vis(ws, child_lod, bx + (c & 1), by + ((c >> 1) & 1),
                                         bz + ((c >> 2) & 1), vis_prims, vis_n)) {
            nk++;
          }
        }
        if (nk == 0) {
          if (ng_rc_ws_apply_split_surface(ws, si, vis_prims, vis_n)) {
            splits++;
            headroom = budget - ng_rc_ws_slot_used_count(ws);
          }
          continue;
        }
        const int cost = nk - 1;
        if (cost > headroom) {
          continue;
        }
        if (ng_rc_ws_apply_split_surface(ws, si, vis_prims, vis_n)) {
          splits++;
          headroom = budget - ng_rc_ws_slot_used_count(ws);
        }
      }
    }
  }

  /* 5) Steal finest only if over budget */
  {
    int used = ng_rc_ws_slot_used_count(ws);
    int excess = used - budget;
    if (excess > 0) {
      int quota = excess < relax_k ? excess : relax_k;
      if (quota < 1) {
        quota = 1;
      }
      ng_rc_ws_steal_finest(ws, quota);
    }
  }

  ws->slot_count = ng_rc_ws_slot_used_count(ws);
}
// agent: composer-2.5 | 2026-08-11 | B66 always-cover collapse tick | c46cdb
// agent: composer-2.5 | 2026-08-11 | B66 want-have balanced depth score | 4f6d2d
// agent: composer-2.5 | 2026-08-11 | GI offline BVH cull foundation | 69e867
// agent: composer-2.5 | 2026-08-11 | cull prims ignore clip volume | 22f8df
// agent: composer-2.5 | 2026-08-11 | octree cover split surface tick | 6ba63e
// agent: composer-2.5 | 2026-08-11 | fair 75pct poorest-mesh split | 844498
// agent: composer-2.5 | 2026-08-11 | surface-area 50pct fair split | b7ee3b
// agent: composer-2.5 | 2026-08-11 | stochastic fair branch split | 9c7027
// agent: composer-2.5 | 2026-08-11 | gen-sync octree waves plus log | 10b75c
// agent: composer-2.5 | 2026-08-11 | unlimit root lod contain AABB | 594f2e
// agent: composer-2.5 | 2026-08-11 | restore cover then gen wave | 8e1bf4
// agent: composer-2.5 | 2026-08-11 | SDF shell keep surface cells only | a394a2
// agent: composer-2.5 | 2026-08-11 | enforce surface budget half pool | de62a7
// agent: composer-2.5 | 2026-08-12 | rebuild_prims store inst_i | 267192
// agent: composer-2.5 | 2026-08-12 | GPU probe tick API | af2c49
// agent: composer-2.5 | 2026-08-12 | probe incremental helpers | a22078
// agent: composer-2.5 | 2026-08-12 | rc-ws playbook GPU probes | 8defb7
// agent: composer-2.5 | 2026-08-12 | docs GPU copies surface split | 89f49d
// agent: composer-2.5 | 2026-08-12 | docs single-root GPU probes | d0ac59
// agent: composer-2.5 | 2026-08-12 | playbook incremental residency | 0f5a93
// agent: grok-4.6 | 2026-08-12 | playbook even split steal | 175820
// agent: grok-4.6 | 2026-08-12 | playbook lazy persistent cover | b407ea
// agent: grok-4.6 | 2026-08-12 | docs CPU oracle smoke | 91ac84
// agent: grok-4.6 | 2026-08-12 | lazy probe CPU oracle tick | 2dd6ca
// agent: grok-4.6 | 2026-08-12 | cover-pressure steal oracle | f7fae3
// agent: grok-4.6 | 2026-08-12 | cover unmet helper API | 5f4345
// agent: grok-4.6 | 2026-08-12 | persist cover-first relax | 052e50
