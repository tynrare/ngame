/*
 * World-space RC geometry + quality (Track A). North star: docs/radiance-cascades-3d.md
 *
 * Gateway role: pattern | Scope id: render-rc | Flow id: rc-ws
 * Related: src/client/render.c (look-at clip before gbuf; fill/merge/SH/resolve)
 * Downstream: res/shaders/rc_ws_fill.fs (march backend)
 *
 * rc-ws flow (current — 6.1):
 * 1) ensure → scratch + tex_vox; quality → N/dirs/cascades/steps
 * 2) render.c → look-at–snapped clip origin/size (before gbuf UVW)
 * 3) dirty → rebuild_vox → AABB stamp → upload tex_vox
 * 4) fill samples tex_vox; merge → SH → soft-nearest resolve (render.c)
 *
 * rc-ws flow (planned — 6.2; see north star):
 * 5) rebuild_prims from mesh_kind + pose + lit (cube/sphere SDF)
 * 6) upload prim list (UBO / texture pack; WebGL2-safe)
 * 7) fill sphere-traces scene SDF; demote tex_vox off product path
 * 8) later 6.3: optional SVO empty-skip / sparse probe keys (not 6.2)
 *
 * Branches / invariants:
 * - Clip anchors on cam.target (not frustum AABB).
 * - Merge/SH/resolve/compose stay cascade-storage; only march backend changes in 6.2.
 */
// agent: composer-2.5 | 2026-08-10 | CPU frustum vox rebuild impl | 689264
// agent: composer-2.5 | 2026-08-10 | rc-ws playbook SDF plan | a4dc86
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
  if (ws->ready && ws->vox_tex_ready && ws->vox_rgba) {
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
  Image img = GenImageColor(NG_RC_WS_VOX_RES, NG_RC_WS_VOX_RES * NG_RC_WS_VOX_RES, BLANK);
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

uint32_t ng_rc_ws_scene_hash(void) {
  uint32_t h = 2166136261u;
  h = ng_rc_ws_hash_u32(h, 0xF1002u);
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

bool ng_rc_ws_inst_stamp(int i, float center[3], float half[3], float lit[3]) {
  if (!center || !half || !lit) {
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
  if (resolved.mesh_kind == NG_SCENE_MESH_SPHERE) {
    hx = hy = hz = resolved.mesh_w * s;
  } else {
    const float k = 1.5f * 0.5f;
    hx = resolved.mesh_w * k * s;
    hy = resolved.mesh_h * k * s;
    hz = resolved.mesh_d * k * s;
  }
  if (hy < 0.35f && hx >= 2.0f && hz >= 2.0f) {
    return false;
  }
  center[0] = inst->pos[0];
  center[1] = inst->pos[1];
  center[2] = inst->pos[2];
  half[0] = hx;
  half[1] = hy;
  half[2] = hz;
  const float ar = (resolved.have_tint ? (float)resolved.tint_r : 180.0f) / 255.0f;
  const float ag = (resolved.have_tint ? (float)resolved.tint_g : 180.0f) / 255.0f;
  const float ab = (resolved.have_tint ? (float)resolved.tint_b : 180.0f) / 255.0f;
  const float er = (resolved.have_glow ? (float)resolved.glow_r : 0.0f) / 255.0f;
  const float eg = (resolved.have_glow ? (float)resolved.glow_g : 0.0f) / 255.0f;
  const float eb = (resolved.have_glow ? (float)resolved.glow_b : 0.0f) / 255.0f;
  const float emit_boost = 2.2f;
  const float bounce = 1.35f;
  lit[0] = er * emit_boost + ar * bounce;
  lit[1] = eg * emit_boost + ag * bounce;
  lit[2] = eb * emit_boost + ab * bounce;
  if (lit[0] > 1.0f) {
    lit[0] = 1.0f;
  }
  if (lit[1] > 1.0f) {
    lit[1] = 1.0f;
  }
  if (lit[2] > 1.0f) {
    lit[2] = 1.0f;
  }
  return true;
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

static void ng_rc_ws_stamp_box(NgRcWsCtx *ws, const float c[3], const float h[3],
                               const float lit[3]) {
  int x0, y0, z0, x1, y1, z1;
  ng_rc_ws_world_to_vox(ws, c[0] - h[0], c[1] - h[1], c[2] - h[2], &x0, &y0, &z0);
  ng_rc_ws_world_to_vox(ws, c[0] + h[0], c[1] + h[1], c[2] + h[2], &x1, &y1, &z1);
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
  const unsigned char r = (unsigned char)(lit[0] * 255.0f);
  const unsigned char g = (unsigned char)(lit[1] * 255.0f);
  const unsigned char b = (unsigned char)(lit[2] * 255.0f);
  for (int z = z0; z <= z1; z++) {
    for (int y = y0; y <= y1; y++) {
      for (int x = x0; x <= x1; x++) {
        const int i = ng_rc_ws_vox_index(x, y, z);
        ws->vox_occ[i] = 255;
        ws->vox_alb[i * 3 + 0] = r;
        ws->vox_alb[i * 3 + 1] = g;
        ws->vox_alb[i * 3 + 2] = b;
      }
    }
  }
}

void ng_rc_ws_rebuild_vox(NgRcWsCtx *ws) {
  if (!ws || !ws->ready || !ws->vox_tex_ready || !ws->vox_rgba) {
    return;
  }
  memset(ws->vox_occ, 0, (size_t)NG_RC_WS_VOX_N);
  memset(ws->vox_alb, 0, (size_t)NG_RC_WS_VOX_N * 3u);
  memset(ws->vox_emit, 0, (size_t)NG_RC_WS_VOX_N * 3u);
  const int n = mod_scene_graph_inst_count();
  for (int i = 0; i < n; i++) {
    float center[3];
    float half[3];
    float lit[3];
    if (!ng_rc_ws_inst_stamp(i, center, half, lit)) {
      continue;
    }
    /* Overlap vs volume */
    const float amin[3] = {center[0] - half[0], center[1] - half[1], center[2] - half[2]};
    const float amax[3] = {center[0] + half[0], center[1] + half[1], center[2] + half[2]};
    const float bmin[3] = {ws->origin[0], ws->origin[1], ws->origin[2]};
    const float bmax[3] = {ws->origin[0] + ws->size[0], ws->origin[1] + ws->size[1],
                           ws->origin[2] + ws->size[2]};
    if (amin[0] > bmax[0] || amax[0] < bmin[0] || amin[1] > bmax[1] || amax[1] < bmin[1] ||
        amin[2] > bmax[2] || amax[2] < bmin[2]) {
      continue;
    }
    ng_rc_ws_stamp_box(ws, center, half, lit);
  }
  for (int z = 0; z < NG_RC_WS_VOX_RES; z++) {
    for (int y = 0; y < NG_RC_WS_VOX_RES; y++) {
      for (int x = 0; x < NG_RC_WS_VOX_RES; x++) {
        const int vi = ng_rc_ws_vox_index(x, y, z);
        const int pi = (z * NG_RC_WS_VOX_RES + y) * NG_RC_WS_VOX_RES + x;
        if (!ws->vox_occ[vi]) {
          ws->vox_rgba[pi * 4 + 0] = 0;
          ws->vox_rgba[pi * 4 + 1] = 0;
          ws->vox_rgba[pi * 4 + 2] = 0;
          ws->vox_rgba[pi * 4 + 3] = 0;
          continue;
        }
        ws->vox_rgba[pi * 4 + 0] = ws->vox_alb[vi * 3 + 0];
        ws->vox_rgba[pi * 4 + 1] = ws->vox_alb[vi * 3 + 1];
        ws->vox_rgba[pi * 4 + 2] = ws->vox_alb[vi * 3 + 2];
        ws->vox_rgba[pi * 4 + 3] = 255;
      }
    }
  }
  UpdateTexture(ws->tex_vox, ws->vox_rgba);
}
// agent: composer-2.5 | 2026-08-10 | CPU frustum vox rebuild impl | 689264
// agent: composer-2.5 | 2026-08-10 | rc-ws playbook SDF plan | a4dc86
