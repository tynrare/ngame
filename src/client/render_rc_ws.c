// agent: grok-4.6 | 2026-08-21 | drop sparse grid probe CPU | f771fd
/*
 * World-space RC geometry + quality. North star: docs/radiance-cascades-3d.md
 *
 * Gateway role: pattern | Scope id: render-rc | Flow id: rc-ws
 * Related: src/client/render.c (CPU cull, chunk cull, gbuf)
 * Downstream: res/shaders/rc_ws_debug.fs, rc_gbuf.vs
 * Debug: set debug.render.pass culling|grid|…
 *
 * rc-ws flow (chunked static grid):
 * 1) ensure → tex_prim + tex_bvh
 * 2) scene dirty → prims + inst_i + CPU BVH
 * 3) CPU BVH frustum cull → inst bitset + UploadTexture tex_prim_vis
 * 4) vis chunks from vis-prim AABBs, cap VIS_MAX
 * 5) bind vis chunks → GPU pages (LRU recycle)
 * 6) dirty pages: fill 2..0 (emit + albedo×prev field; sky/sun on C2 miss),
 *    store C2, merge C1/C0 4:1 dirs, store C0, encode SH for bounce
 * 7) gbuf; resolve cosine C0 → rt_ws; compose kd*E_rc
 *
 * Branches / invariants:
 * - World-fixed chunks; UVW perfect hash h; no look-at clip; no sparse slots.
 * - Page identity is (cx,cy,cz); probe id is (cx,cy,cz,h). Page index is not id.
 * - Chunk work only for vis-bound pages.
 * - Fill hit = emit×boost + albedo×prev field (not albedo-as-lamp).
 * - Live: dirs ×4, 4:1 merge, cosine C0; C2 miss sky+sun lobe; compose N·L is not GI.
 */
// agent: grok-4.6 | 2026-08-21 | playbook C2 sun miss live | 0afdf1
// agent: grok-4.6 | 2026-08-21 | playbook fill merge resolve | 5fd1c9
// agent: grok-4.6 | 2026-08-21 | playbook persist coarse merge | 717840
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
// agent: composer-2.5 | 2026-08-13 | BVH scale cull 2048 | c4e91a
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
#define NG_RC_WS_AABB_PAD_SCALE 1.12f

// agent: grok-4.6 | 2026-08-21 | drop sparse grid probe CPU | f771fd
void ng_rc_ws_init(NgRcWsCtx *ws) {
  if (!ws) {
    return;
  }
  memset(ws, 0, sizeof(*ws));
  ws->bvh_root = -1;
  ws->cull_valid = false;
  for (int i = 0; i < NG_RC_WS_INST_MAX; i++) {
    ws->inst_prim[i] = -1;
  }
}

// agent: grok-4.6 | 2026-08-21 | drop sparse grid probe CPU | f771fd
void ng_rc_ws_shutdown(NgRcWsCtx *ws) {
  if (!ws) {
    return;
  }
  if (ws->prim_tex_ready) {
    UnloadTexture(ws->tex_prim);
    ws->prim_tex_ready = false;
  }
  if (ws->bvh_tex_ready) {
    UnloadTexture(ws->tex_bvh);
    ws->bvh_tex_ready = false;
  }
  if (ws->inst_prim_tex_ready) {
    UnloadTexture(ws->tex_inst_prim);
    ws->inst_prim_tex_ready = false;
  }
  if (ws->chunk_vis_tex_ready) {
    UnloadTexture(ws->tex_chunk_vis);
    ws->chunk_vis_tex_ready = false;
  }
  if (ws->pages_tex_ready) {
    UnloadTexture(ws->tex_pages);
    ws->pages_tex_ready = false;
  }
  if (ws->page_hash_tex_ready) {
    UnloadTexture(ws->tex_page_hash);
    ws->page_hash_tex_ready = false;
  }
  free(ws->prim_rgba);
  ws->prim_rgba = NULL;
  free(ws->bvh_rgba);
  ws->bvh_rgba = NULL;
  free(ws->inst_prim_rgba);
  ws->inst_prim_rgba = NULL;
  free(ws->prim_vis_rgba);
  ws->prim_vis_rgba = NULL;
  free(ws->chunk_vis_rgba);
  ws->chunk_vis_rgba = NULL;
  free(ws->pages_rgba);
  ws->pages_rgba = NULL;
  free(ws->page_hash_rgba);
  ws->page_hash_rgba = NULL;
  ws->prim_count = 0;
  ws->cull_valid = false;
  ws->ready = false;
}

// agent: grok-4.6 | 2026-08-21 | drop sparse grid probe CPU | f771fd
static bool ng_rc_ws_alloc(NgRcWsCtx *ws) {
  if (ws->ready && ws->prim_tex_ready && ws->prim_rgba && ws->bvh_tex_ready && ws->bvh_rgba &&
      ws->inst_prim_tex_ready && ws->inst_prim_rgba && ws->chunk_vis_rgba &&
      ws->chunk_vis_tex_ready && ws->pages_rgba && ws->pages_tex_ready && ws->page_hash_rgba &&
      ws->page_hash_tex_ready) {
    return true;
  }
  if (!ws->prim_rgba) {
    ws->prim_rgba = (float *)calloc((size_t)NG_RC_WS_PRIM_FLOATS, sizeof(float));
  }
  if (!ws->bvh_rgba) {
    ws->bvh_rgba = (float *)calloc((size_t)NG_RC_WS_BVH_FLOATS, sizeof(float));
  }
  if (!ws->inst_prim_rgba) {
    ws->inst_prim_rgba = (float *)calloc((size_t)NG_RC_WS_INST_PRIM_FLOATS, sizeof(float));
  }
  if (!ws->prim_vis_rgba) {
    ws->prim_vis_rgba = (unsigned char *)calloc((size_t)NG_RC_WS_PRIM_VIS_BYTES, 1);
  }
  if (!ws->chunk_vis_rgba) {
    ws->chunk_vis_rgba = (float *)calloc((size_t)NG_RC_WS_CHUNK_VIS_MAX * 4u, sizeof(float));
  }
  if (!ws->pages_rgba) {
    ws->pages_rgba = (float *)calloc((size_t)NG_RC_WS_PAGE_CAP * 4u, sizeof(float));
  }
  if (!ws->page_hash_rgba) {
    ws->page_hash_rgba = (float *)calloc((size_t)NG_RC_WS_PAGE_HASH * 4u, sizeof(float));
  }
  if (!ws->prim_rgba || !ws->bvh_rgba || !ws->inst_prim_rgba || !ws->prim_vis_rgba ||
      !ws->chunk_vis_rgba || !ws->pages_rgba || !ws->page_hash_rgba) {
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
  if (!ws->inst_prim_tex_ready) {
    Image iimg = {0};
    iimg.data = ws->inst_prim_rgba;
    iimg.width = 1;
    iimg.height = NG_RC_WS_INST_MAX;
    iimg.mipmaps = 1;
    iimg.format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;
    ws->tex_inst_prim = LoadTextureFromImage(iimg);
    iimg.data = NULL;
    UnloadImage(iimg);
    if (ws->tex_inst_prim.id == 0) {
      ng_rc_ws_shutdown(ws);
      return false;
    }
    SetTextureFilter(ws->tex_inst_prim, TEXTURE_FILTER_POINT);
    SetTextureWrap(ws->tex_inst_prim, TEXTURE_WRAP_CLAMP);
    ws->inst_prim_tex_ready = true;
  }
  if (!ws->chunk_vis_tex_ready) {
    Image cimg = {0};
    cimg.data = ws->chunk_vis_rgba;
    cimg.width = 1;
    cimg.height = NG_RC_WS_CHUNK_VIS_MAX;
    cimg.mipmaps = 1;
    cimg.format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;
    ws->tex_chunk_vis = LoadTextureFromImage(cimg);
    cimg.data = NULL;
    UnloadImage(cimg);
    if (ws->tex_chunk_vis.id == 0) {
      ng_rc_ws_shutdown(ws);
      return false;
    }
    SetTextureFilter(ws->tex_chunk_vis, TEXTURE_FILTER_POINT);
    SetTextureWrap(ws->tex_chunk_vis, TEXTURE_WRAP_CLAMP);
    ws->chunk_vis_tex_ready = true;
  }
  if (!ws->pages_tex_ready) {
    Image pimg = {0};
    pimg.data = ws->pages_rgba;
    pimg.width = 1;
    pimg.height = NG_RC_WS_PAGE_CAP;
    pimg.mipmaps = 1;
    pimg.format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;
    ws->tex_pages = LoadTextureFromImage(pimg);
    pimg.data = NULL;
    UnloadImage(pimg);
    if (ws->tex_pages.id == 0) {
      ng_rc_ws_shutdown(ws);
      return false;
    }
    SetTextureFilter(ws->tex_pages, TEXTURE_FILTER_POINT);
    SetTextureWrap(ws->tex_pages, TEXTURE_WRAP_CLAMP);
    ws->pages_tex_ready = true;
  }
  if (!ws->page_hash_tex_ready) {
    Image himg = {0};
    himg.data = ws->page_hash_rgba;
    himg.width = NG_RC_WS_PAGE_HASH;
    himg.height = 1;
    himg.format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;
    himg.mipmaps = 1;
    ws->tex_page_hash = LoadTextureFromImage(himg);
    himg.data = NULL;
    UnloadImage(himg);
    if (ws->tex_page_hash.id == 0) {
      ng_rc_ws_shutdown(ws);
      return false;
    }
    SetTextureFilter(ws->tex_page_hash, TEXTURE_FILTER_POINT);
    SetTextureWrap(ws->tex_page_hash, TEXTURE_WRAP_CLAMP);
    ws->page_hash_tex_ready = true;
  }
  ws->ready = true;
  return true;
}

// agent: grok-4.6 | 2026-08-21 | drop sparse grid probe CPU | f771fd
bool ng_rc_ws_ensure(NgRcWsCtx *ws, int quality) {
  (void)quality;
  if (!ws) {
    return false;
  }
  return ng_rc_ws_alloc(ws);
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


static void ng_rc_ws_bvh_rebuild(NgRcWsCtx *ws);

// agent: grok-4.6 | 2026-08-21 | drop sparse grid probe CPU | f771fd
void ng_rc_ws_rebuild_prims(NgRcWsCtx *ws) {
  if (!ws || !ws->ready || !ws->prim_tex_ready || !ws->prim_rgba) {
    return;
  }
  ws->prim_count = 0;
  ws->cull_valid = false;
  memset(ws->prim_rgba, 0, (size_t)NG_RC_WS_PRIM_FLOATS * sizeof(float));
  for (int i = 0; i < NG_RC_WS_INST_MAX; i++) {
    ws->inst_prim[i] = -1;
  }
  const int n = mod_scene_graph_inst_count();
  for (int i = 0; i < n && ws->prim_count < NG_RC_WS_PRIM_MAX; i++) {
    NgRcWsPrim p;
    if (!ng_rc_ws_inst_prim(i, &p)) {
      continue;
    }
    const int row = ws->prim_count;
    p.inst_i = i;
    p.leaf_node = -1;
    ws->prims[row] = p;
    ws->inst_prim[i] = (int16_t)row;
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
    rowf[23] = (float)(i + 1);
    rowf[24] = 0.0f;
    rowf[25] = 0.0f;
    rowf[26] = 0.0f;
    rowf[27] = 1.0f;
    ws->prim_count++;
  }
  UpdateTexture(ws->tex_prim, ws->prim_rgba);
  ng_rc_ws_bvh_rebuild(ws);
  for (int pi = 0; pi < ws->prim_count; pi++) {
    float *rowf = ws->prim_rgba + pi * NG_RC_WS_PRIM_COLS * 4;
    rowf[24] = (float)(ws->prims[pi].leaf_node + 1);
  }
  UpdateTexture(ws->tex_prim, ws->prim_rgba);
  ng_rc_ws_upload_bvh(ws);
  ng_rc_ws_upload_inst_prim(ws);
  TraceLog(LOG_INFO, "rc-ws prims rebuild count=%d bvh=%d depth=%d", ws->prim_count, ws->bvh_count,
           ws->bvh_max_depth);
}

/** Pack CPU BVH nodes into tex_bvh (cols: bmin+left, bmax+right, prim+parent, depth). */
void ng_rc_ws_upload_bvh(NgRcWsCtx *ws) {
  // agent: composer-2.5 | 2026-08-13 | BVH parent depth upload | b7a2c0
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
    row[9] = (float)nd->parent;
    row[10] = 0.0f;
    row[11] = 1.0f;
    row[12] = (float)nd->depth;
    row[13] = 0.0f;
    row[14] = 0.0f;
    row[15] = 1.0f;
  }
  UpdateTexture(ws->tex_bvh, ws->bvh_rgba);
}

/** Upload inst→prim map (cold only). */
void ng_rc_ws_upload_inst_prim(NgRcWsCtx *ws) {
  // agent: composer-2.5 | 2026-08-13 | inst prim map cold upload | d3f8e1
  if (!ws || !ws->inst_prim_tex_ready || !ws->inst_prim_rgba) {
    return;
  }
  memset(ws->inst_prim_rgba, 0, (size_t)NG_RC_WS_INST_PRIM_FLOATS * sizeof(float));
  const int n = mod_scene_graph_inst_count();
  const int cap = n < NG_RC_WS_INST_MAX ? n : NG_RC_WS_INST_MAX;
  for (int i = 0; i < cap; i++) {
    const int pi = (int)ws->inst_prim[i];
    ws->inst_prim_rgba[i * 4] = pi >= 0 ? (float)(pi + 1) : 0.0f;
    ws->inst_prim_rgba[i * 4 + 3] = 1.0f;
  }
  UpdateTexture(ws->tex_inst_prim, ws->inst_prim_rgba);
}

/** World AABB for analytic prim (bound sphere padded). */
static void ng_rc_ws_prim_aabb(const NgRcWsPrim *p, float bmin[3], float bmax[3]) {
  const float r = ng_rc_ws_prim_bound_r(p) * NG_RC_WS_AABB_PAD_SCALE;
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
static int ng_rc_ws_bvh_build_range(NgRcWsCtx *ws, int *idx, int n, int parent, int depth) {
  // agent: composer-2.5 | 2026-08-13 | BVH parent depth links | e8d4f2
  if (n <= 0 || ws->bvh_count >= NG_RC_WS_BVH_MAX) {
    return -1;
  }
  const int node = ws->bvh_count++;
  NgRcWsBvhNode *nd = &ws->bvh[node];
  nd->left = -1;
  nd->right = -1;
  nd->prim = -1;
  nd->parent = parent;
  nd->depth = depth;
  if (depth > ws->bvh_max_depth) {
    ws->bvh_max_depth = depth;
  }
  if (n == 1) {
    const int pi = idx[0];
    nd->prim = pi;
    ng_rc_ws_prim_aabb(&ws->prims[pi], nd->bmin, nd->bmax);
    ws->prims[pi].leaf_node = node;
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
    ws->prims[idx[0]].leaf_node = node;
    ng_rc_ws_prim_aabb(&ws->prims[idx[0]], nd->bmin, nd->bmax);
    return node;
  }
  nd->left = ng_rc_ws_bvh_build_range(ws, idx, mid, node, depth + 1);
  nd->right = ng_rc_ws_bvh_build_range(ws, idx + mid, n - mid, node, depth + 1);
  return node;
}

static void ng_rc_ws_bvh_rebuild(NgRcWsCtx *ws) {
  // agent: composer-2.5 | 2026-08-13 | BVH parent depth rebuild | a1b3c5
  ws->bvh_count = 0;
  ws->bvh_root = -1;
  ws->bvh_max_depth = 0;
  if (!ws || ws->prim_count <= 0) {
    return;
  }
  int idx[NG_RC_WS_PRIM_MAX];
  for (int i = 0; i < ws->prim_count; i++) {
    idx[i] = i;
    ws->prims[i].leaf_node = -1;
  }
  ws->bvh_root = ng_rc_ws_bvh_build_range(ws, idx, ws->prim_count, -1, 0);
}

/** True if AABB fully outside any frustum plane. */
static bool ng_rc_ws_aabb_outside_frustum(const float bmin[3], const float bmax[3],
                                          const Vector4 planes[6]) {
  // agent: composer-2.5 | 2026-08-13 | CPU BVH traverse prim vis | 7fc2f1
  for (int i = 0; i < 6; i++) {
    const Vector4 p = planes[i];
    const float px = p.x > 0.0f ? bmax[0] : bmin[0];
    const float py = p.y > 0.0f ? bmax[1] : bmin[1];
    const float pz = p.z > 0.0f ? bmax[2] : bmin[2];
    if (p.x * px + p.y * py + p.z * pz + p.w < -NG_RC_WS_AABB_PAD) {
      return true;
    }
  }
  return false;
}

/** CPU BVH stack traverse; sets inst bitset + cull_prim_vis. */
int ng_rc_ws_cull_traverse(NgRcWsCtx *ws, const Vector4 planes[6], int *vis_inst, int vis_cap) {
  // agent: composer-2.5 | 2026-08-13 | CPU BVH traverse prim vis | 7fc2f1
  ws->cull_valid = false;
  ws->cull_vis_n = 0;
  memset(ws->cull_vis_bits, 0, sizeof(ws->cull_vis_bits));
  memset(ws->cull_prim_vis, 0, sizeof(ws->cull_prim_vis));
  if (!ws || ws->bvh_root < 0 || ws->prim_count <= 0) {
    return 0;
  }
  int stack[NG_RC_WS_CULL_STACK];
  int sp = 0;
  stack[sp++] = ws->bvh_root;
  int vis_n = 0;
  while (sp > 0) {
    const int node_i = stack[--sp];
    if (node_i < 0 || node_i >= ws->bvh_count) {
      continue;
    }
    const NgRcWsBvhNode *nd = &ws->bvh[node_i];
    float bmin[3] = {nd->bmin[0], nd->bmin[1], nd->bmin[2]};
    float bmax[3] = {nd->bmax[0], nd->bmax[1], nd->bmax[2]};
    if (ng_rc_ws_aabb_outside_frustum(bmin, bmax, planes)) {
      continue;
    }
    if (nd->prim >= 0) {
      const int pi = nd->prim;
      if (pi >= 0 && pi < NG_RC_WS_PRIM_MAX) {
        ws->cull_prim_vis[pi] = 1;
      }
      const int inst = ws->prims[pi].inst_i;
      if (inst >= 0 && inst < NG_RC_WS_INST_MAX) {
        ws->cull_vis_bits[inst >> 5] |= (1u << (inst & 31));
        if (vis_inst && vis_n < vis_cap) {
          vis_inst[vis_n++] = inst;
        }
      }
      continue;
    }
    if (nd->right >= 0 && sp < NG_RC_WS_CULL_STACK) {
      stack[sp++] = nd->right;
    }
    if (nd->left >= 0 && sp < NG_RC_WS_CULL_STACK) {
      stack[sp++] = nd->left;
    }
  }
  ws->cull_vis_n = vis_n;
  ws->cull_valid = true;
  return vis_n;
}

/** Pack CPU prim vis → tex_prim_vis (R=255 visible). */
void ng_rc_ws_upload_prim_vis(NgRcWsCtx *ws, Texture2D tex) {
  // agent: composer-2.5 | 2026-08-13 | CPU BVH traverse prim vis | 7fc2f1
  if (!ws || !ws->prim_vis_rgba || tex.id == 0) {
    return;
  }
  memset(ws->prim_vis_rgba, 0, (size_t)NG_RC_WS_PRIM_VIS_BYTES);
  const int pc = ws->prim_count < NG_RC_WS_PRIM_MAX ? ws->prim_count : NG_RC_WS_PRIM_MAX;
  const int row_bytes = NG_RC_WS_PRIM_MAX * 4;
  for (int pi = 0; pi < pc; pi++) {
    if (!ws->cull_prim_vis[pi]) {
      continue;
    }
    for (int row = 0; row < 2; row++) {
      unsigned char *px = ws->prim_vis_rgba + row * row_bytes + pi * 4;
      px[0] = 255;
      px[1] = (unsigned char)((pi + 1) & 255);
      px[3] = 255;
    }
  }
  UpdateTexture(tex, ws->prim_vis_rgba);
}

/** True if inst passed last CPU cull traverse. */
bool ng_rc_ws_inst_visible(const NgRcWsCtx *ws, int inst_i) {
  if (!ws || !ws->cull_valid) {
    return true;
  }
  if (inst_i < 0 || inst_i >= NG_RC_WS_INST_MAX) {
    return false;
  }
  if (ws->inst_prim[inst_i] < 0) {
    return true;
  }
  return (ws->cull_vis_bits[inst_i >> 5] >> (inst_i & 31)) & 1u;
}

// agent: grok-4.6 | 2026-08-21 | chunk lattice helpers | 7079c7
float ng_rc_ws_chunk_extent(void) {
  return NG_RC_WS_CELL * (float)NG_RC_WS_CHUNK_N;
}

void ng_rc_ws_world_to_chunk(const float p[3], int32_t *cx, int32_t *cy, int32_t *cz) {
  const float e = ng_rc_ws_chunk_extent();
  if (!p || e < 1e-8f) {
    if (cx) {
      *cx = 0;
    }
    if (cy) {
      *cy = 0;
    }
    if (cz) {
      *cz = 0;
    }
    return;
  }
  if (cx) {
    *cx = (int32_t)floorf(p[0] / e);
  }
  if (cy) {
    *cy = (int32_t)floorf(p[1] / e);
  }
  if (cz) {
    *cz = (int32_t)floorf(p[2] / e);
  }
}

int ng_rc_ws_chunk_h(int ix, int iy, int iz) {
  const int n = NG_RC_WS_CHUNK_N;
  if (ix < 0) {
    ix = 0;
  } else if (ix >= n) {
    ix = n - 1;
  }
  if (iy < 0) {
    iy = 0;
  } else if (iy >= n) {
    iy = n - 1;
  }
  if (iz < 0) {
    iz = 0;
  } else if (iz >= n) {
    iz = n - 1;
  }
  return ix + n * (iy + n * iz);
}

void ng_rc_ws_h_to_ijk(int h, int *ix, int *iy, int *iz) {
  const int n = NG_RC_WS_CHUNK_N;
  const int cells = NG_RC_WS_CHUNK_CELLS;
  if (h < 0) {
    h = 0;
  } else if (h >= cells) {
    h = cells - 1;
  }
  if (ix) {
    *ix = h % n;
  }
  if (iy) {
    *iy = (h / n) % n;
  }
  if (iz) {
    *iz = h / (n * n);
  }
}

void ng_rc_ws_chunk_aabb(int32_t cx, int32_t cy, int32_t cz, float bmin[3], float bmax[3]) {
  const float e = ng_rc_ws_chunk_extent();
  if (!bmin || !bmax) {
    return;
  }
  bmin[0] = (float)cx * e;
  bmin[1] = (float)cy * e;
  bmin[2] = (float)cz * e;
  bmax[0] = bmin[0] + e;
  bmax[1] = bmin[1] + e;
  bmax[2] = bmin[2] + e;
}

void ng_rc_ws_cell_center(int32_t cx, int32_t cy, int32_t cz, int h, float out[3]) {
  int ix = 0;
  int iy = 0;
  int iz = 0;
  const float cell = NG_RC_WS_CELL;
  if (!out) {
    return;
  }
  ng_rc_ws_h_to_ijk(h, &ix, &iy, &iz);
  float bmin[3];
  float bmax[3];
  ng_rc_ws_chunk_aabb(cx, cy, cz, bmin, bmax);
  out[0] = bmin[0] + ((float)ix + 0.5f) * cell;
  out[1] = bmin[1] + ((float)iy + 0.5f) * cell;
  out[2] = bmin[2] + ((float)iz + 0.5f) * cell;
}

static int ng_rc_ws_chunk_find(const NgRcWsCtx *ws, int32_t cx, int32_t cy, int32_t cz) {
  const int n = ws->chunk_vis_n;
  for (int i = 0; i < n; i++) {
    const NgRcWsChunkId *c = &ws->chunk_vis[i];
    if (c->cx == cx && c->cy == cy && c->cz == cz) {
      return i;
    }
  }
  return -1;
}

static int ng_rc_ws_aabb_overlap(const float amin[3], const float amax[3], const float bmin[3],
                                 const float bmax[3]) {
  return amin[0] <= bmax[0] && amax[0] >= bmin[0] && amin[1] <= bmax[1] && amax[1] >= bmin[1] &&
         amin[2] <= bmax[2] && amax[2] >= bmin[2];
}

static void ng_rc_ws_upload_chunk_vis(NgRcWsCtx *ws) {
  if (!ws || !ws->chunk_vis_rgba) {
    return;
  }
  memset(ws->chunk_vis_rgba, 0, (size_t)NG_RC_WS_CHUNK_VIS_MAX * 4u * sizeof(float));
  const int n = ws->chunk_vis_n < NG_RC_WS_CHUNK_VIS_MAX ? ws->chunk_vis_n : NG_RC_WS_CHUNK_VIS_MAX;
  for (int i = 0; i < n; i++) {
    ws->chunk_vis_rgba[i * 4 + 0] = (float)ws->chunk_vis[i].cx;
    ws->chunk_vis_rgba[i * 4 + 1] = (float)ws->chunk_vis[i].cy;
    ws->chunk_vis_rgba[i * 4 + 2] = (float)ws->chunk_vis[i].cz;
    ws->chunk_vis_rgba[i * 4 + 3] = 1.0f;
  }
  if (ws->chunk_vis_tex_ready) {
    UpdateTexture(ws->tex_chunk_vis, ws->chunk_vis_rgba);
  }
}

static void ng_rc_ws_upload_pages(NgRcWsCtx *ws) {
  if (!ws || !ws->pages_rgba) {
    return;
  }
  memset(ws->pages_rgba, 0, (size_t)NG_RC_WS_PAGE_CAP * 4u * sizeof(float));
  for (int i = 0; i < NG_RC_WS_PAGE_CAP; i++) {
    if (!ws->pages[i].occupied) {
      continue;
    }
    ws->pages_rgba[i * 4 + 0] = (float)ws->pages[i].cx;
    ws->pages_rgba[i * 4 + 1] = (float)ws->pages[i].cy;
    ws->pages_rgba[i * 4 + 2] = (float)ws->pages[i].cz;
    ws->pages_rgba[i * 4 + 3] = 1.0f;
  }
  if (ws->pages_tex_ready) {
    UpdateTexture(ws->tex_pages, ws->pages_rgba);
  }
  // agent: grok-4.6 | 2026-08-21 | page hash table upload | 704884
  if (ws->page_hash_rgba) {
    memset(ws->page_hash_rgba, 0, (size_t)NG_RC_WS_PAGE_HASH * 4u * sizeof(float));
    for (int i = 0; i < NG_RC_WS_PAGE_CAP; i++) {
      if (!ws->pages[i].occupied) {
        continue;
      }
      const uint32_t hx = (uint32_t)ws->pages[i].cx * 73856093u;
      const uint32_t hy = (uint32_t)ws->pages[i].cy * 19349663u;
      const uint32_t hz = (uint32_t)ws->pages[i].cz * 83492791u;
      const uint32_t h0 = hx ^ hy ^ hz;
      int placed = 0;
      for (int p = 0; p < NG_RC_WS_PAGE_HASH_PROBE; p++) {
        const int slot = (int)((h0 + (uint32_t)p) & (uint32_t)(NG_RC_WS_PAGE_HASH - 1));
        float *row = ws->page_hash_rgba + slot * 4;
        if (row[3] < 0.5f) {
          row[0] = (float)ws->pages[i].cx;
          row[1] = (float)ws->pages[i].cy;
          row[2] = (float)ws->pages[i].cz;
          row[3] = (float)(i + 1);
          placed = 1;
          break;
        }
      }
      (void)placed;
    }
    if (ws->page_hash_tex_ready) {
      UpdateTexture(ws->tex_page_hash, ws->page_hash_rgba);
    }
  }
}

static int ng_rc_ws_page_find(const NgRcWsCtx *ws, int32_t cx, int32_t cy, int32_t cz) {
  for (int i = 0; i < NG_RC_WS_PAGE_CAP; i++) {
    if (ws->pages[i].occupied && ws->pages[i].cx == cx && ws->pages[i].cy == cy &&
        ws->pages[i].cz == cz) {
      return i;
    }
  }
  return -1;
}

static int ng_rc_ws_page_evict(NgRcWsCtx *ws) {
  int victim = -1;
  uint32_t oldest = 0xffffffffu;
  for (int i = 0; i < NG_RC_WS_PAGE_CAP; i++) {
    if (!ws->pages[i].occupied) {
      return i;
    }
    if (ng_rc_ws_chunk_find(ws, ws->pages[i].cx, ws->pages[i].cy, ws->pages[i].cz) >= 0) {
      continue;
    }
    if (ws->pages[i].last_use <= oldest) {
      oldest = ws->pages[i].last_use;
      victim = i;
    }
  }
  if (victim >= 0) {
    return victim;
  }
  oldest = 0xffffffffu;
  for (int i = 0; i < NG_RC_WS_PAGE_CAP; i++) {
    if (ws->pages[i].last_use <= oldest) {
      oldest = ws->pages[i].last_use;
      victim = i;
    }
  }
  return victim;
}

void ng_rc_ws_page_bind(NgRcWsCtx *ws) {
  // agent: grok-4.6 | 2026-08-21 | chunk page pool LRU bind | 815dfe
  if (!ws) {
    return;
  }
  ws->page_tick++;
  const int n = ws->chunk_vis_n < NG_RC_WS_CHUNK_VIS_MAX ? ws->chunk_vis_n : NG_RC_WS_CHUNK_VIS_MAX;
  for (int i = 0; i < n; i++) {
    const NgRcWsChunkId *c = &ws->chunk_vis[i];
    int pi = ng_rc_ws_page_find(ws, c->cx, c->cy, c->cz);
    if (pi < 0) {
      pi = ng_rc_ws_page_evict(ws);
      if (pi < 0) {
        continue;
      }
      ws->pages[pi].cx = c->cx;
      ws->pages[pi].cy = c->cy;
      ws->pages[pi].cz = c->cz;
      ws->pages[pi].occupied = 1;
      ws->pages[pi].dirty = 1;
    }
    ws->pages[pi].last_use = ws->page_tick;
  }
  ng_rc_ws_upload_pages(ws);
}

void ng_rc_ws_pages_mark_dirty(NgRcWsCtx *ws) {
  // agent: grok-4.6 | 2026-08-21 | page dirty on rebind | ad6ff0
  if (!ws) {
    return;
  }
  for (int i = 0; i < NG_RC_WS_PAGE_CAP; i++) {
    if (ws->pages[i].occupied) {
      ws->pages[i].dirty = 1;
    }
  }
}

/** Visible chunks from vis-prim AABBs (no extra frustum — prims already culled). */
void ng_rc_ws_chunk_cull(NgRcWsCtx *ws, const Vector4 planes[6]) {
  // agent: grok-4.6 | 2026-08-21 | chunk vis tex no extra frustum | 00a346
  (void)planes;
  if (!ws) {
    return;
  }
  ws->chunk_vis_n = 0;
  if (ws->prim_count <= 0) {
    ng_rc_ws_upload_chunk_vis(ws);
    ng_rc_ws_page_bind(ws);
    return;
  }
  const float e = ng_rc_ws_chunk_extent();
  if (e < 1e-8f) {
    ng_rc_ws_upload_chunk_vis(ws);
    ng_rc_ws_page_bind(ws);
    return;
  }
  const int pc = ws->prim_count < NG_RC_WS_PRIM_MAX ? ws->prim_count : NG_RC_WS_PRIM_MAX;
  for (int pi = 0; pi < pc && ws->chunk_vis_n < NG_RC_WS_CHUNK_VIS_MAX; pi++) {
    if (!ws->cull_prim_vis[pi]) {
      continue;
    }
    float pmin[3];
    float pmax[3];
    ng_rc_ws_prim_aabb(&ws->prims[pi], pmin, pmax);
    const int32_t cx0 = (int32_t)floorf(pmin[0] / e);
    const int32_t cy0 = (int32_t)floorf(pmin[1] / e);
    const int32_t cz0 = (int32_t)floorf(pmin[2] / e);
    const int32_t cx1 = (int32_t)floorf(pmax[0] / e);
    const int32_t cy1 = (int32_t)floorf(pmax[1] / e);
    const int32_t cz1 = (int32_t)floorf(pmax[2] / e);
    for (int32_t cz = cz0; cz <= cz1 && ws->chunk_vis_n < NG_RC_WS_CHUNK_VIS_MAX; cz++) {
      for (int32_t cy = cy0; cy <= cy1 && ws->chunk_vis_n < NG_RC_WS_CHUNK_VIS_MAX; cy++) {
        for (int32_t cx = cx0; cx <= cx1 && ws->chunk_vis_n < NG_RC_WS_CHUNK_VIS_MAX; cx++) {
          if (ng_rc_ws_chunk_find(ws, cx, cy, cz) >= 0) {
            continue;
          }
          float bmin[3];
          float bmax[3];
          ng_rc_ws_chunk_aabb(cx, cy, cz, bmin, bmax);
          if (!ng_rc_ws_aabb_overlap(bmin, bmax, pmin, pmax)) {
            continue;
          }
          NgRcWsChunkId *dst = &ws->chunk_vis[ws->chunk_vis_n++];
          dst->cx = cx;
          dst->cy = cy;
          dst->cz = cz;
        }
      }
    }
  }
  ng_rc_ws_upload_chunk_vis(ws);
  ng_rc_ws_page_bind(ws);
}
// agent: grok-4.6 | 2026-08-21 | playbook C2 sun miss live | 0afdf1
// agent: grok-4.6 | 2026-08-21 | playbook live cosine C0 | 6d7d8e
// agent: grok-4.6 | 2026-08-21 | playbook live 4to1 merge | 3d55ab
// agent: grok-4.6 | 2026-08-21 | playbook RC dirs merge target | f1df5b
// agent: grok-4.6 | 2026-08-21 | playbook fill bounce sky | 7ffb85
// agent: grok-4.6 | 2026-08-21 | drop sparse grid probe CPU | f771fd
// agent: composer-2.5 | 2026-08-13 | BVH scale cull 2048 | c4e91a
// agent: composer-2.5 | 2026-08-11 | B66 want-have balanced depth score | 4f6d2d
// agent: grok-4.6 | 2026-08-21 | drop sparse grid probe CPU | f771fd
