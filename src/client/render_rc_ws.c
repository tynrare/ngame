/*
 * World-space RC: AABB voxelize → vox slice atlas for GPU dir-packed RC.
 *
 * Gateway role: pattern | Scope id: render-rc | Flow id: rc-ws
 * Related: src/client/render.c (GPU fill/merge/resolve/compose)
 *
 * rc-ws flow:
 * 1) ensure → allocate vox scratch + tex_vox; quality → N/dirs/cascades/steps
 * 2) sync_vox → stamp graph AABBs when scene_hash dirty
 * 3) upload_vox → RGBA slice atlas for GPU march
 * Invariant: quality scales cost only. No SH. XZ flatland is not this path.
 * GPU (render.c): casc fill → T-merge → N·ω screen resolve → compose.
 */
// agent: composer-2.5 | 2026-08-10 | WS vox sync upload | 6edec0
// agent: composer-2.5 | 2026-08-10 | playbook notes GPU resolve path | fd74c2
// agent: composer-2.5 | 2026-08-10 | WS vox texture nearest | 9da03a
// agent: composer-2.5 | 2026-08-10 | vox AABB match mesh 1.5 | 1e7753
// agent: composer-2.5 | 2026-08-10 | skip floor slab voxelize | 2f28b8
#include "render_rc_ws.h"
#include "scene/assets.h"
#include "scene/graph.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NG_RC_WS_VOX_N (NG_RC_WS_VOX_RES * NG_RC_WS_VOX_RES * NG_RC_WS_VOX_RES)

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
  ws->probe_n = 12;
  ws->dirs = 8;
  ws->cascades = 3;
  ws->steps = 5;
}

void ng_rc_ws_shutdown(NgRcWsCtx *ws) {
  if (!ws) {
    return;
  }
  if (ws->vox_tex_ready) {
    UnloadTexture(ws->tex_vox);
    ws->vox_tex_ready = false;
  }
  free(ws->vox_rgba);
  free(ws->vox_occ);
  free(ws->vox_alb);
  free(ws->vox_emit);
  ws->vox_rgba = NULL;
  ws->vox_occ = NULL;
  ws->vox_alb = NULL;
  ws->vox_emit = NULL;
  ws->ready = false;
}

static bool ng_rc_ws_alloc(NgRcWsCtx *ws) {
  if (ws->ready) {
    return true;
  }
  ws->vox_occ = (unsigned char *)calloc((size_t)NG_RC_WS_VOX_N, 1);
  ws->vox_alb = (unsigned char *)calloc((size_t)NG_RC_WS_VOX_N * 3u, 1);
  ws->vox_emit = (unsigned char *)calloc((size_t)NG_RC_WS_VOX_N * 3u, 1);
  ws->vox_rgba = (unsigned char *)calloc((size_t)NG_RC_WS_VOX_N * 4u, 1);
  if (!ws->vox_occ || !ws->vox_alb || !ws->vox_emit || !ws->vox_rgba) {
    ng_rc_ws_shutdown(ws);
    return false;
  }
  Image img = GenImageColor(NG_RC_WS_VOX_RES, NG_RC_WS_VOX_RES * NG_RC_WS_VOX_RES, BLACK);
  ws->tex_vox = LoadTextureFromImage(img);
  UnloadImage(img);
  if (ws->tex_vox.id == 0) {
    ng_rc_ws_shutdown(ws);
    return false;
  }
  SetTextureFilter(ws->tex_vox, TEXTURE_FILTER_POINT);
  ws->vox_tex_ready = true;
  ws->ready = true;
  return true;
}

bool ng_rc_ws_ensure(NgRcWsCtx *ws, int quality) {
  static const int k_probe[5] = {8, 10, 12, 16, 16};
  /* More dirs (same probe_n) — kills star/ray artifacts; cost scales with dirs. */
  // agent: composer-2.5 | 2026-08-10 | raise WS dirs quality ladder | 0478ff
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
  ws->probe_n = k_probe[q];
  ws->dirs = k_dirs[q];
  ws->cascades = k_cascades[q];
  ws->steps = k_steps[q];
  return true;
}

static uint32_t ng_rc_ws_hash_u32(uint32_t h, uint32_t v) {
  h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
  return h;
}

static uint32_t ng_rc_ws_scene_hash(void) {
  uint32_t h = 2166136261u;
  h = ng_rc_ws_hash_u32(h, 0xF1002u); /* floor-slab skip policy */
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
    h = ng_rc_ws_hash_u32(h, (uint32_t)(inst->scale * 100.0f));
  }
  return h;
}

static int ng_rc_ws_vox_index(int x, int y, int z) {
  return (z * NG_RC_WS_VOX_RES + y) * NG_RC_WS_VOX_RES + x;
}

static void ng_rc_ws_world_to_vox(const NgRcWsCtx *ws, float wx, float wy, float wz, int *ox,
                                  int *oy, int *oz) {
  *ox = (int)floorf((wx - ws->origin[0]) / ws->size[0] * (float)NG_RC_WS_VOX_RES);
  *oy = (int)floorf((wy - ws->origin[1]) / ws->size[1] * (float)NG_RC_WS_VOX_RES);
  *oz = (int)floorf((wz - ws->origin[2]) / ws->size[2] * (float)NG_RC_WS_VOX_RES);
}

static void ng_rc_ws_stamp_box(NgRcWsCtx *ws, float cx, float cy, float cz, float hx, float hy,
                               float hz, unsigned char ar, unsigned char ag, unsigned char ab,
                               unsigned char er, unsigned char eg, unsigned char eb) {
  int x0, y0, z0, x1, y1, z1;
  ng_rc_ws_world_to_vox(ws, cx - hx, cy - hy, cz - hz, &x0, &y0, &z0);
  ng_rc_ws_world_to_vox(ws, cx + hx, cy + hy, cz + hz, &x1, &y1, &z1);
  if (x0 > x1) {
    int t = x0;
    x0 = x1;
    x1 = t;
  }
  if (y0 > y1) {
    int t = y0;
    y0 = y1;
    y1 = t;
  }
  if (z0 > z1) {
    int t = z0;
    z0 = z1;
    z1 = t;
  }
  if (x1 < 0 || y1 < 0 || z1 < 0 || x0 >= NG_RC_WS_VOX_RES || y0 >= NG_RC_WS_VOX_RES ||
      z0 >= NG_RC_WS_VOX_RES) {
    return;
  }
  if (x0 < 0) {
    x0 = 0;
  }
  if (y0 < 0) {
    y0 = 0;
  }
  if (z0 < 0) {
    z0 = 0;
  }
  if (x1 >= NG_RC_WS_VOX_RES) {
    x1 = NG_RC_WS_VOX_RES - 1;
  }
  if (y1 >= NG_RC_WS_VOX_RES) {
    y1 = NG_RC_WS_VOX_RES - 1;
  }
  if (z1 >= NG_RC_WS_VOX_RES) {
    z1 = NG_RC_WS_VOX_RES - 1;
  }
  for (int z = z0; z <= z1; z++) {
    for (int y = y0; y <= y1; y++) {
      for (int x = x0; x <= x1; x++) {
        const int i = ng_rc_ws_vox_index(x, y, z);
        ws->vox_occ[i] = 255;
        ws->vox_alb[i * 3 + 0] = ar;
        ws->vox_alb[i * 3 + 1] = ag;
        ws->vox_alb[i * 3 + 2] = ab;
        if (er | eg | eb) {
          ws->vox_emit[i * 3 + 0] = er;
          ws->vox_emit[i * 3 + 1] = eg;
          ws->vox_emit[i * 3 + 2] = eb;
        }
      }
    }
  }
}

static void ng_rc_ws_voxelize(NgRcWsCtx *ws) {
  memset(ws->vox_occ, 0, (size_t)NG_RC_WS_VOX_N);
  memset(ws->vox_alb, 0, (size_t)NG_RC_WS_VOX_N * 3u);
  memset(ws->vox_emit, 0, (size_t)NG_RC_WS_VOX_N * 3u);
  const int n = mod_scene_graph_inst_count();
  for (int i = 0; i < n; i++) {
    const NgSceneInst *inst = mod_scene_graph_inst_at(i);
    if (!inst || !inst->alive || !inst->model[0]) {
      continue;
    }
    NgSceneResolvedModel resolved;
    if (!mod_scene_assets_resolve_model(inst->model, &resolved) || !resolved.ok) {
      continue;
    }
    const float s = inst->scale > 0.0f ? inst->scale : 1.0f;
    float hx;
    float hy;
    float hz;
    if (resolved.mesh_kind == NG_SCENE_MESH_SPHERE) {
      /* GenMeshSphere(mesh_w) — radius = mesh_w. */
      hx = hy = hz = resolved.mesh_w * s;
    } else {
      /* GenMeshCube(w*1.5,…) — half-extent = mesh_* * 0.75 * s. */
      const float k = 1.5f * 0.5f;
      hx = resolved.mesh_w * k * s;
      hy = resolved.mesh_h * k * s;
      hz = resolved.mesh_d * k * s;
    }
    // agent: composer-2.5 | 2026-08-10 | skip floor slab voxelize | 2f28b8
    /* Huge thin floors block all wall rays from floor probes — skip for GI vox. */
    if (hy < 0.35f && hx >= 2.0f && hz >= 2.0f) {
      continue;
    }
    const unsigned char ar = resolved.have_tint ? resolved.tint_r : 180;
    const unsigned char ag = resolved.have_tint ? resolved.tint_g : 180;
    const unsigned char ab = resolved.have_tint ? resolved.tint_b : 180;
    const unsigned char er = resolved.have_glow ? resolved.glow_r : 0;
    const unsigned char eg = resolved.have_glow ? resolved.glow_g : 0;
    const unsigned char eb = resolved.have_glow ? resolved.glow_b : 0;
    ng_rc_ws_stamp_box(ws, inst->pos[0], inst->pos[1], inst->pos[2], hx, hy, hz, ar, ag, ab, er, eg,
                       eb);
  }
}

bool ng_rc_ws_sync_vox(NgRcWsCtx *ws) {
  if (!ws || !ws->ready) {
    return false;
  }
  const uint32_t h = ng_rc_ws_scene_hash();
  if (h != ws->scene_hash || ws->frames_since_vox > 30) {
    ng_rc_ws_voxelize(ws);
    ws->scene_hash = h;
    ws->frames_since_vox = 0;
    return true;
  }
  ws->frames_since_vox++;
  return false;
}

void ng_rc_ws_upload_vox(NgRcWsCtx *ws) {
  if (!ws || !ws->ready || !ws->vox_tex_ready || !ws->vox_rgba) {
    return;
  }
  const float emit_boost = 2.2f;
  const float bounce = 1.35f;
  for (int z = 0; z < NG_RC_WS_VOX_RES; z++) {
    for (int y = 0; y < NG_RC_WS_VOX_RES; y++) {
      for (int x = 0; x < NG_RC_WS_VOX_RES; x++) {
        const int vi = ng_rc_ws_vox_index(x, y, z);
        /* Atlas: width=VOX, height=VOX*VOX; slice z stacked in Y. */
        const int pi = (z * NG_RC_WS_VOX_RES + y) * NG_RC_WS_VOX_RES + x;
        if (!ws->vox_occ[vi]) {
          ws->vox_rgba[pi * 4 + 0] = 0;
          ws->vox_rgba[pi * 4 + 1] = 0;
          ws->vox_rgba[pi * 4 + 2] = 0;
          ws->vox_rgba[pi * 4 + 3] = 0;
          continue;
        }
        float r = (float)ws->vox_emit[vi * 3 + 0] / 255.0f * emit_boost +
                  (float)ws->vox_alb[vi * 3 + 0] / 255.0f * bounce;
        float g = (float)ws->vox_emit[vi * 3 + 1] / 255.0f * emit_boost +
                  (float)ws->vox_alb[vi * 3 + 1] / 255.0f * bounce;
        float b = (float)ws->vox_emit[vi * 3 + 2] / 255.0f * emit_boost +
                  (float)ws->vox_alb[vi * 3 + 2] / 255.0f * bounce;
        if (r > 1.0f) {
          r = 1.0f;
        }
        if (g > 1.0f) {
          g = 1.0f;
        }
        if (b > 1.0f) {
          b = 1.0f;
        }
        ws->vox_rgba[pi * 4 + 0] = (unsigned char)(r * 255.0f);
        ws->vox_rgba[pi * 4 + 1] = (unsigned char)(g * 255.0f);
        ws->vox_rgba[pi * 4 + 2] = (unsigned char)(b * 255.0f);
        ws->vox_rgba[pi * 4 + 3] = 255;
      }
    }
  }
  UpdateTexture(ws->tex_vox, ws->vox_rgba);
}
// agent: composer-2.5 | 2026-08-10 | WS vox sync upload | 6edec0
// agent: composer-2.5 | 2026-08-10 | WS vox texture nearest | 9da03a
// agent: composer-2.5 | 2026-08-10 | vox AABB match mesh 1.5 | 1e7753
// agent: composer-2.5 | 2026-08-10 | skip floor slab voxelize | 2f28b8
// agent: composer-2.5 | 2026-08-10 | raise WS dirs quality ladder | 0478ff
// agent: composer-2.5 | 2026-08-10 | playbook notes GPU resolve path | fd74c2
