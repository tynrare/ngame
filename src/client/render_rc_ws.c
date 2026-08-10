/*
 * World-space RC geometry + quality (Track A). North star: docs/radiance-cascades-3d.md
 *
 * Gateway role: pattern | Scope id: render-rc | Flow id: rc-ws
 * Related: src/client/render.c (look-at clip before gbuf; fill/merge/SH/resolve)
 * Downstream: res/shaders/rc_ws_fill.fs (march backend)
 *
 * rc-ws flow:
 * 1) ensure → tex_prim; quality → N/dirs/cascades/steps
 * 2) render.c → look-at–snapped clip origin/size (before gbuf UVW)
 * 3) dirty → rebuild_prims (all mesh entities; pose/quat/lit/PBR) → upload tex_prim
 * 4) fill sphere-traces analytic SDF; merge → SH → soft-nearest resolve
 *
 * Branches / invariants:
 * - Clip anchors on cam.target (not frustum AABB).
 * - Every described mesh/entity is a prim (no floor special-case).
 * - Prim pose matches DrawMeshInstanced (euler→quat, uniform scale).
 * - Dense tex_vox demoted; product path is SDF prims only.
 */
// agent: composer-2.5 | 2026-08-10 | demote vox drop floor skip | 0c617e
#include "render_rc_ws.h"
#include "scene/assets.h"
#include "scene/graph.h"
#include <raymath.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NG_RC_WS_PRIM_FLOATS (NG_RC_WS_PRIM_MAX * NG_RC_WS_PRIM_COLS * 4)

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
  if (ws->prim_tex_ready) {
    UnloadTexture(ws->tex_prim);
    ws->prim_tex_ready = false;
  }
  free(ws->prim_rgba);
  ws->prim_rgba = NULL;
  ws->prim_count = 0;
  ws->ready = false;
}

/** Allocate prim GPU/CPU scratch. */
static bool ng_rc_ws_alloc(NgRcWsCtx *ws) {
  if (ws->ready && ws->prim_tex_ready && ws->prim_rgba) {
    return true;
  }
  if (!ws->prim_rgba) {
    ws->prim_rgba = (float *)calloc((size_t)NG_RC_WS_PRIM_FLOATS, sizeof(float));
  }
  if (!ws->prim_rgba) {
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
    // agent: composer-2.5 | 2026-08-10 | prim pack emit in col3 | 3eafc7
    rowf[0] = p.center[0];
    rowf[1] = p.center[1];
    rowf[2] = p.center[2];
    rowf[3] = (float)p.type;
    rowf[4] = p.half[0];
    rowf[5] = p.half[1];
    rowf[6] = p.half[2];
    rowf[7] = 1.0f;
    rowf[8] = p.quat[0];
    rowf[9] = p.quat[1];
    rowf[10] = p.quat[2];
    rowf[11] = p.quat[3];
    /* col3: emit + roughness (fill radiance; was lit-clamped→grey for dark tint) */
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
// agent: composer-2.5 | 2026-08-10 | demote vox drop floor skip | 0c617e
// agent: composer-2.5 | 2026-08-10 | prim pack emit in col3 | 3eafc7
