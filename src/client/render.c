// agent: composer-2.5 | 2026-07-25 | client render module | g0j28e
// agent: composer-2.5 | 2026-07-28 | render drop embedded path | f42f1c
// agent: composer-2.5 | 2026-08-09 | shader glow rough metal uniforms | 7e0b28
// agent: composer-2.5 | 2026-08-09 | gbuffer RTs debug blit | 96d6a0
// agent: composer-2.5 | 2026-08-09 | instanced draw batch pools | 8837bc
// agent: composer-2.5 | 2026-08-09 | Phase2 SS RC render path | ff1b7f
// agent: composer-2.5 | 2026-08-09 | gate RC by scene render mode | 2a6d5a
// agent: composer-2.5 | 2026-08-09 | gi_strength CLI render | a5a86e
// agent: composer-2.5 | 2026-08-09 | default gi_strength 1.0 | 6674ef
// agent: composer-2.5 | 2026-08-09 | CLI set render.rc tree | 28de10
// agent: composer-2.5 | 2026-08-09 | pass under debug.render | c088d1
// agent: composer-2.5 | 2026-08-09 | present RT for render.scale | 2c679e
// agent: composer-2.5 | 2026-08-09 | fix present FBO nesting | c075d5
// agent: composer-2.5 | 2026-08-09 | wire WS RC quality path | d68d00
// agent: composer-2.5 | 2026-08-09 | uniform WS quality cost scale | 45673f
// agent: composer-2.5 | 2026-08-12 | deferred prim probe id passes | f9cfb4
// agent: composer-2.5 | 2026-08-12 | VS cull double-buffer vis | aac081
// agent: composer-2.5 | 2026-08-12 | persistent probe fingerprint | 00025d
// agent: composer-2.5 | 2026-08-12 | GPU probe apply no readback | fc3300
// agent: composer-2.5 | 2026-08-09 | Phase2 GPU FBO RC path | 5642b8
// agent: composer-2.5 | 2026-08-09 | WS then SS pipeline wire | ed5dfb
// agent: composer-2.5 | 2026-08-09 | stamp seed prop butter order | b9a6eb
// agent: composer-2.5 | 2026-08-09 | XZ splat emitters-only stamp | bd73fb
// agent: composer-2.5 | 2026-08-09 | WS fs draw no Y flip | 07886c
// agent: composer-2.5 | 2026-08-09 | wire WS radial RC | 18204b
// agent: composer-2.5 | 2026-08-09 | half-res WS casc bilinear merge | 187201
// agent: composer-2.5 | 2026-08-09 | WS blit use raylib Y flip | 7f49b1
// agent: composer-2.5 | 2026-08-09 | wire SS packed RC | 7ba28e
// agent: composer-2.5 | 2026-08-10 | RC cost shift dirs cut | d9bceb
// agent: composer-2.5 | 2026-08-10 | wire WS volume RC tick | 0fd9cd
// agent: composer-2.5 | 2026-08-10 | WS atlas nearest no Vflip | 787633
// agent: composer-2.5 | 2026-08-10 | WS blit identity no flip | b8e489
// agent: composer-2.5 | 2026-08-10 | depth far WS simplify | 6dd473
// agent: composer-2.5 | 2026-08-10 | ss weight zero isolate WS | b458b3
// agent: composer-2.5 | 2026-08-10 | negate cam_right reconstruct | 95d5a9
// agent: composer-2.5 | 2026-08-10 | wire ws origin gbuf world | 4bc5ef
// agent: composer-2.5 | 2026-08-10 | gbuf disable blend pack | 8655f8
// agent: composer-2.5 | 2026-08-10 | SS fill world dist uniforms | 9b6064
// agent: composer-2.5 | 2026-08-10 | B3 sparse tick seed fill | 4d7b85
// agent: composer-2.5 | 2026-08-10 | B3 seed blit quiet readback | 034eb0
// agent: composer-2.5 | 2026-08-10 | B3 fill skip clear dirty only | 93345e
// agent: composer-2.5 | 2026-08-10 | ws fill disable blend dirty | 466455
// agent: composer-2.5 | 2026-08-10 | merge pingpong not casc | 27d0fe
// agent: composer-2.5 | 2026-08-10 | LOD bands from camera eye | 4e0c43
// agent: composer-2.5 | 2026-08-11 | B62 skip fill if no dirty | bcd897
// agent: composer-2.5 | 2026-08-11 | B64 wire GPU prio passes | f7b538
// agent: composer-2.5 | 2026-08-11 | BVH frustum cull foundation wire | 868ee4
#include "render.h"
#include "render_rc_ws.h"
#include "engine/ng_action.h"
#include "engine/ng_bus.h"
#include "client/input.h"
#include "net/mod_net.h"
#include "scene/scene.h"
#include "scene/runtime.h"
#include "scene/graph.h"
#include "scene/assets.h"
#include "scene/native.h"
#include "ng_path.h"
#include "ng_shader.h"
#include "ng_viewport.h"
#include "world/ng_world.h"
#include <math.h>
#include <limits.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum NgRenderDebugPass {
  NG_RENDER_PASS_FINAL = 0,
  NG_RENDER_PASS_ALBEDO,
  NG_RENDER_PASS_NORMAL,
  NG_RENDER_PASS_GLOW,
  NG_RENDER_PASS_DEPTH,
  NG_RENDER_PASS_IRRADIANCE,
  // agent: composer-2.5 | 2026-08-10 | debug probes grid atlas wire | 49402d
  NG_RENDER_PASS_UVW,
  NG_RENDER_PASS_PROBES,
  // agent: composer-2.5 | 2026-08-12 | add probes-lod debug pass | b65b50
  NG_RENDER_PASS_PROBES_LOD,
  NG_RENDER_PASS_GRID,
  NG_RENDER_PASS_ATLAS,
  NG_RENDER_PASS_CULLING,
} NgRenderDebugPass;

typedef struct RenderAsset {
  bool ready;
  Model model;
  NgShader shader;
  Color bg;
  Color tint;
  Color glow;
  float roughness;
  float metalness;
} RenderAsset;

#define NG_RENDER_CACHE_MAX 16
#define NG_RENDER_BATCH_MAX 16

typedef struct NgInstanceBatch {
  char model[32];
  Matrix *mats;
  int count;
  int capacity;
} NgInstanceBatch;

typedef struct RenderAssetCacheEntry {
  char key[32];
  RenderAsset asset;
} RenderAssetCacheEntry;

typedef struct NgRcPassShader {
  NgShader sh;
  int loc_tex_depth;
  int loc_tex_albedo;
  int loc_tex_glow;
  int loc_tex_normal;
  int loc_tex_irradiance;
  int loc_tex_irradiance_ws;
  int loc_tex_irradiance_ss;
  int loc_tex_self;
  int loc_tex_parent;
  int loc_tex_prev;
  int loc_tex_curr;
  int loc_tex_atlas;
  int loc_tex_stamp;
  int loc_sky;
  int loc_gi_strength;
  int loc_ws_weight;
  int loc_ss_weight;
  int loc_dir_count;
  int loc_dir_side;
  int loc_spacing;
  int loc_interval0;
  int loc_interval1;
  int loc_max_steps;
  int loc_merge_weight;
  int loc_butter;
  int loc_texel;
  int loc_spread;
  int loc_parent_texel;
  int loc_cascade_res;
  int loc_probes;
  int loc_parent_probes;
  int loc_parent_res;
  int loc_parent_dir_side;
  int loc_cam_pos;
  int loc_cam_forward;
  int loc_cam_right;
  int loc_cam_up;
  int loc_tan_half_fov;
  int loc_aspect;
  int loc_ws_origin;
  int loc_ws_size;
  int loc_probe_res;
  bool ready;
} NgRcPassShader;

#define NG_RC_CASCADES_MAX 3
#define NG_RC_WS_ATLAS_MAX 128
#define NG_RC_WS_CLIP_NEAR 0
#define NG_RC_WS_CLIP_FAR 1
#define NG_RC_WS_CLIP_COUNT 2

/** One look-at–locked clipmap volume (near or far). */
typedef struct NgRcWsClip {
  float origin[3];
  float size[3];
  float snap_origin[3];
  float snap_size[3];
  RenderTexture2D casc[NG_RC_CASCADES_MAX];
  RenderTexture2D stamp;  /* merge ping A — never write casc[] */
  RenderTexture2D stamp2; /* merge ping B */
  RenderTexture2D sh;
} NgRcWsClip;

typedef struct ModRenderCtx {
  RenderAssetCacheEntry cache[NG_RENDER_CACHE_MAX];
  int cache_count;
  NgInstanceBatch batches[NG_RENDER_BATCH_MAX];
  int batch_count;
  NgSnapshot prev;
  NgSnapshot curr;
  bool have_prev;
  bool have_curr;
  float alpha;
  char scene_label[32];
  bool have_session;
  NgSyncMode scene_sync;
  Camera3D camera;
  uint16_t last_input_seq;
  NgRenderDebugPass debug_pass;
  // agent: composer-2.5 | 2026-08-13 | opt-in probe occupancy log | f07898
  bool debug_logging_probes;
  int rc_quality;
  float gi_strength;
  // agent: composer-2.5 | 2026-08-10 | ws ss weight defaults | 1b1432
  float ws_weight;
  float ss_weight;
  float render_scale;
  RenderTexture2D rt_present;
  bool present_ready;
  int present_w;
  int present_h;
  RenderTexture2D rt_albedo;
  RenderTexture2D rt_normal;
  RenderTexture2D rt_glow;
  RenderTexture2D rt_depth;
  RenderTexture2D rt_prim_id;
  RenderTexture2D rt_probe_id;
  bool gbuf_ready;
  int gbuf_w;
  int gbuf_h;
  NgShader gbuf_shader;
  bool gbuf_shader_ready;
  RenderTexture2D rt_cascade[NG_RC_CASCADES_MAX];
  RenderTexture2D rt_merge;
  RenderTexture2D rt_ss_irr;
  bool rc_rt_ready;
  int rc_w;
  int rc_h;
  int rc_cascades;
  int rc_dir_side[NG_RC_CASCADES_MAX];
  int rc_probes_x[NG_RC_CASCADES_MAX];
  int rc_probes_y[NG_RC_CASCADES_MAX];
  int rc_spacing[NG_RC_CASCADES_MAX];
  /* WS RC: GPU cull (+ look-at clip when GI on) + sparse pool + screen irr. */
  // agent: composer-2.5 | 2026-08-11 | wipe look-at cube cull path | bf3c1d
  // agent: composer-2.5 | 2026-08-10 | B1 dual clip near far RTs | d8ac7a
  // agent: composer-2.5 | 2026-08-10 | B3 sparse tick seed fill | 4d7b85
  // agent: composer-2.5 | 2026-08-11 | B6 kill seed readback tick | 7c8edc
  NgRcWsClip ws_clip[NG_RC_WS_CLIP_COUNT];
  RenderTexture2D rt_ws[2];
  RenderTexture2D rt_vox; /* VOX × VOX² frustum-fit stamp atlas */
  bool vox_rt_ready;
  uint32_t ws_vox_scene_hash;
  int ws_ping;
  // agent: composer-2.5 | 2026-08-10 | B2 amortize far fill cadence | e09d1a
  int ws_frame;
  bool ws_rt_ready;
  int ws_probe_n;
  int ws_dirs;
  int ws_irr_w;
  int ws_irr_h;
  NgRcWsCtx ws_cpu;
  NgRcPassShader rc_fill;
  NgRcPassShader rc_merge;
  NgRcPassShader rc_resolve;
  NgRcPassShader rc_compose;
  NgRcPassShader rc_ws_butter;
  NgRcPassShader rc_ws_fill;
  NgRcPassShader rc_ws_merge;
  NgRcPassShader rc_ws_sh_encode;
  NgRcPassShader rc_ws_resolve;
  NgRcPassShader rc_ws_view;
  // agent: composer-2.5 | 2026-08-10 | debug probes grid atlas wire | 49402d
  NgRcPassShader rc_ws_debug;
  NgRcPassShader rc_ws_id;
  NgRcPassShader rc_ws_vox_stamp;
  // agent: composer-2.5 | 2026-08-11 | B64 wire GPU prio passes | f7b538
  NgRcPassShader rc_ws_prio;
  NgRcPassShader rc_ws_prio_reduce;
  RenderTexture2D rt_ws_op; /* 1×1 reduce target for K-list picks */
  bool ws_prio_gpu_ready;
  // agent: composer-2.5 | 2026-08-13 | CPU cull tick upload vis | 4d378d
  RenderTexture2D rt_prim_vis; /* PRIM_MAX × 2 prim frustum flags (curr) */
  RenderTexture2D rt_prim_vis_prev; /* previous frame prim vis */
  bool ws_cull_ready;
  Vector4 ws_frustum[6];
  bool ws_frustum_valid;
  uint32_t probe_fp;
  bool probe_gpu_valid;
  NgRcPassShader rc_ws_probe;
  NgRcPassShader rc_ws_probe_cover;
  NgRcPassShader rc_ws_probe_apply;
  NgRcPassShader rc_ws_probe_gen;
  // agent: composer-2.5 | 2026-08-12 | probe incremental residency tick | 5af26a
  NgRcPassShader rc_ws_probe_union;
  NgRcPassShader rc_ws_probe_release;
  NgRcPassShader rc_ws_probe_merge;
  NgRcPassShader rc_ws_probe_steal;
  NgRcPassShader rc_ws_probe_stats;
  NgRcPassShader rc_ws_probe_unmet;
  NgRcPassShader rc_ws_probe_relax;
  RenderTexture2D rt_probe_work; /* WORK_MAX × 1 float work cells */
  RenderTexture2D rt_probe_keep; /* WORK_MAX × 2 keep flags */
  RenderTexture2D rt_probe_slots; /* slot_cap × 1 ix,iy,iz,lod+1 */
  // agent: composer-2.5 | 2026-08-12 | slots ping-pong for split | f33dd4
  RenderTexture2D rt_probe_slots_b; /* ping-pong for split-merge */
  RenderTexture2D rt_probe_meta; /* 1 × slot_cap center+occ */
  RenderTexture2D rt_probe_hash; /* hash_size × 1 */
  RenderTexture2D rt_probe_union; /* 2×1 vis-union umin/umax */
  RenderTexture2D rt_probe_stats; /* 1×1 used/freeable/budget */
  RenderTexture2D rt_probe_unmet; /* 1×1 cover unmet flag */
  bool ws_probe_ready;
  uint32_t probe_frame; /* stochastic lottery seed */
} ModRenderCtx;

static ModRenderCtx g_render_ctx;
static const Vector3 NG_RC_SKY = {0.08f, 0.10f, 0.14f};

/** Load RGBA32F color + depth renderbuffer FBO (world XYZ packing). */
static RenderTexture2D mod_render_load_rt_rgba32f(int width, int height);

/** Internal RT size from viewport × render_scale (clamped). */
static void mod_render_internal_size(const ModRenderCtx *ctx, int *out_w, int *out_h) {
  float s = ctx->render_scale;
  if (s < 0.25f) {
    s = 0.25f;
  } else if (s > 2.0f) {
    s = 2.0f;
  }
  int w = (int)((float)ng_viewport_width() * s + 0.5f);
  int h = (int)((float)ng_viewport_height() * s + 0.5f);
  if (w < 1) {
    w = 1;
  }
  if (h < 1) {
    h = 1;
  }
  *out_w = w;
  *out_h = h;
}

static void mod_render_blit_rt(const RenderTexture2D *rt);

static const char *mod_render_pass_name(NgRenderDebugPass pass) {
  switch (pass) {
  case NG_RENDER_PASS_ALBEDO:
    return "albedo";
  case NG_RENDER_PASS_NORMAL:
    return "normal";
  case NG_RENDER_PASS_GLOW:
    return "glow";
  case NG_RENDER_PASS_DEPTH:
    return "depth";
  case NG_RENDER_PASS_IRRADIANCE:
    return "irradiance";
  // agent: composer-2.5 | 2026-08-10 | debug probes grid atlas wire | 49402d
  case NG_RENDER_PASS_UVW:
    return "uvw";
  case NG_RENDER_PASS_PROBES:
    return "probes";
  // agent: composer-2.5 | 2026-08-12 | add probes-lod debug pass | b65b50
  case NG_RENDER_PASS_PROBES_LOD:
    return "probes-lod";
  case NG_RENDER_PASS_GRID:
    return "grid";
  case NG_RENDER_PASS_ATLAS:
    return "atlas";
  case NG_RENDER_PASS_CULLING:
    return "culling";
  case NG_RENDER_PASS_FINAL:
  default:
    return "final";
  }
}

static bool mod_render_pass_from_name(const char *name, NgRenderDebugPass *out) {
  if (!name || !out) {
    return false;
  }
  if (strcmp(name, "final") == 0) {
    *out = NG_RENDER_PASS_FINAL;
    return true;
  }
  if (strcmp(name, "albedo") == 0) {
    *out = NG_RENDER_PASS_ALBEDO;
    return true;
  }
  if (strcmp(name, "normal") == 0) {
    *out = NG_RENDER_PASS_NORMAL;
    return true;
  }
  if (strcmp(name, "glow") == 0) {
    *out = NG_RENDER_PASS_GLOW;
    return true;
  }
  if (strcmp(name, "depth") == 0) {
    *out = NG_RENDER_PASS_DEPTH;
    return true;
  }
  if (strcmp(name, "irradiance") == 0) {
    *out = NG_RENDER_PASS_IRRADIANCE;
    return true;
  }
  // agent: composer-2.5 | 2026-08-10 | debug probes grid atlas wire | 49402d
  if (strcmp(name, "uvw") == 0) {
    *out = NG_RENDER_PASS_UVW;
    return true;
  }
  if (strcmp(name, "probes") == 0) {
    *out = NG_RENDER_PASS_PROBES;
    return true;
  }
  // agent: composer-2.5 | 2026-08-12 | add probes-lod debug pass | b65b50
  if (strcmp(name, "probes-lod") == 0) {
    *out = NG_RENDER_PASS_PROBES_LOD;
    return true;
  }
  if (strcmp(name, "grid") == 0) {
    *out = NG_RENDER_PASS_GRID;
    return true;
  }
  if (strcmp(name, "atlas") == 0) {
    *out = NG_RENDER_PASS_ATLAS;
    return true;
  }
  if (strcmp(name, "culling") == 0) {
    *out = NG_RENDER_PASS_CULLING;
    return true;
  }
  return false;
}

static void mod_render_load_asset_mesh(RenderAsset *a, const NgSceneResolvedModel *resolved,
                                       const char *fs_path, const char *vs_path) {
  if (a->ready) {
    return;
  }
  Mesh mesh;
  if (resolved->mesh_kind == NG_SCENE_MESH_SPHERE) {
    mesh = GenMeshSphere(resolved->mesh_w, 32, 32);
  } else {
    mesh = GenMeshCube(resolved->mesh_w * 1.5f, resolved->mesh_h * 1.5f, resolved->mesh_d * 1.5f);
  }
  if (resolved->have_tint) {
    a->tint = (Color){resolved->tint_r, resolved->tint_g, resolved->tint_b, 255};
  } else {
    a->tint = WHITE;
  }
  // agent: composer-2.5 | 2026-08-09 | shader glow rough metal uniforms | 7e0b28
  if (resolved->have_glow) {
    a->glow = (Color){resolved->glow_r, resolved->glow_g, resolved->glow_b, 255};
  } else {
    a->glow = (Color){0, 0, 0, 255};
  }
  a->roughness = resolved->roughness;
  a->metalness = resolved->metalness;
  a->bg = BLACK;
  a->model = LoadModelFromMesh(mesh);
  a->shader = ng_shader_load(vs_path, fs_path);
  if (a->shader.handle.id == 0) {
    UnloadModel(a->model);
    return;
  }
  a->model.materials[0].shader = a->shader.handle;
  a->ready = true;
}

static void mod_render_unload_asset(RenderAsset *a) {
  if (!a->ready) {
    return;
  }
  ng_shader_unload(&a->shader);
  UnloadModel(a->model);
  a->ready = false;
}

static void mod_render_clear_batches(ModRenderCtx *ctx) {
  // agent: composer-2.5 | 2026-08-09 | instanced draw batch pools | 8837bc
  for (int i = 0; i < ctx->batch_count; i++) {
    free(ctx->batches[i].mats);
    ctx->batches[i].mats = NULL;
    ctx->batches[i].count = 0;
    ctx->batches[i].capacity = 0;
    ctx->batches[i].model[0] = '\0';
  }
  ctx->batch_count = 0;
}

static void mod_render_clear_cache(ModRenderCtx *ctx) {
  for (int i = 0; i < ctx->cache_count; i++) {
    mod_render_unload_asset(&ctx->cache[i].asset);
  }
  ctx->cache_count = 0;
  mod_render_clear_batches(ctx);
}

static void mod_render_batches_reset_counts(ModRenderCtx *ctx) {
  for (int i = 0; i < ctx->batch_count; i++) {
    ctx->batches[i].count = 0;
  }
}

/** Grow batch capacity to at least need (start 1, double). */
static bool mod_render_batch_ensure(NgInstanceBatch *b, int need) {
  if (!b || need <= 0) {
    return false;
  }
  if (need <= b->capacity) {
    return true;
  }
  int cap = b->capacity > 0 ? b->capacity : 1;
  while (cap < need) {
    if (cap > INT_MAX / 2) {
      return false;
    }
    cap *= 2;
  }
  Matrix *next = (Matrix *)realloc(b->mats, (size_t)cap * sizeof(Matrix));
  if (!next) {
    return false;
  }
  b->mats = next;
  b->capacity = cap;
  return true;
}

static NgInstanceBatch *mod_render_batch_get(ModRenderCtx *ctx, const char *key) {
  if (!ctx || !key || key[0] == '\0') {
    return NULL;
  }
  for (int i = 0; i < ctx->batch_count; i++) {
    if (strcmp(ctx->batches[i].model, key) == 0) {
      return &ctx->batches[i];
    }
  }
  if (ctx->batch_count >= NG_RENDER_BATCH_MAX) {
    return NULL;
  }
  NgInstanceBatch *b = &ctx->batches[ctx->batch_count++];
  memset(b, 0, sizeof(*b));
  strncpy(b->model, key, sizeof(b->model) - 1);
  return b;
}

/** Append one instance matrix; false on OOM or full batch table. */
static bool mod_render_batch_push(NgInstanceBatch *b, Matrix m) {
  if (!b) {
    return false;
  }
  if (!mod_render_batch_ensure(b, b->count + 1)) {
    return false;
  }
  b->mats[b->count++] = m;
  return true;
}

/** Build world matrix from pose (quat × scale × translate). */
static Matrix mod_render_pose_matrix(float x, float y, float z, const float rot[3], float scale) {
  const float s = scale > 0.0f ? scale : 1.0f;
  const Quaternion q = QuaternionFromEuler(rot[0], rot[1], rot[2]);
  return MatrixMultiply(MatrixMultiply(MatrixScale(s, s, s), QuaternionToMatrix(q)),
                        MatrixTranslate(x, y, z));
}

static RenderAsset *mod_render_cache_get(ModRenderCtx *ctx, const char *key) {
  for (int i = 0; i < ctx->cache_count; i++) {
    if (strcmp(ctx->cache[i].key, key) == 0) {
      return ctx->cache[i].asset.ready ? &ctx->cache[i].asset : NULL;
    }
  }
  return NULL;
}

static RenderAsset *mod_render_cache_put(ModRenderCtx *ctx, const char *key,
                                         const NgSceneResolvedModel *resolved) {
  if (ctx->cache_count >= NG_RENDER_CACHE_MAX) {
    return NULL;
  }
  char fs_path[128];
  char vs_path[128];
  snprintf(fs_path, sizeof(fs_path), NG_RES_ROOT "%s",
           resolved->fragment[0] == '/' ? resolved->fragment + 1 : resolved->fragment);
  snprintf(vs_path, sizeof(vs_path), NG_RES_ROOT "%s",
           resolved->vertex[0] == '/' ? resolved->vertex + 1 : resolved->vertex);
  RenderAssetCacheEntry *entry = &ctx->cache[ctx->cache_count++];
  strncpy(entry->key, key, sizeof(entry->key) - 1);
  mod_render_load_asset_mesh(&entry->asset, resolved, fs_path, vs_path);
  return entry->asset.ready ? &entry->asset : NULL;
}

static RenderAsset *mod_render_asset_for_model(ModRenderCtx *ctx, const char *model_name) {
  mod_scene_runtime_use_view();
  if (!model_name || model_name[0] == '\0') {
    return NULL;
  }
  RenderAsset *cached = mod_render_cache_get(ctx, model_name);
  if (cached) {
    return cached;
  }
  NgSceneResolvedModel resolved;
  if (!mod_scene_assets_resolve_model(model_name, &resolved) || !resolved.ok) {
    return NULL;
  }
  return mod_render_cache_put(ctx, model_name, &resolved);
}

static RenderAsset *mod_render_asset_for_mesh_kind(ModRenderCtx *ctx, NgSceneMeshKind kind) {
  mod_scene_runtime_use_view();
  char key[16];
  snprintf(key, sizeof(key), "@%d", (int)kind);
  RenderAsset *cached = mod_render_cache_get(ctx, key);
  if (cached) {
    return cached;
  }
  NgSceneResolvedModel resolved;
  if (!mod_scene_assets_resolve_model_for_mesh_kind(kind, &resolved) || !resolved.ok) {
    return NULL;
  }
  return mod_render_cache_put(ctx, key, &resolved);
}

static Color mod_render_bg_color(void) {
  mod_scene_runtime_use_view();
  const NgSceneViewMeta *view = mod_scene_assets_view();
  if (view && view->valid) {
    return (Color){view->bg_r, view->bg_g, view->bg_b, 255};
  }
  return (Color){12, 20, 48, 255};
}

static void mod_render_init_camera(ModRenderCtx *ctx) {
  ctx->camera = (Camera3D){
      .position = (Vector3){0.0f, 2.0f, 6.0f},
      .target = (Vector3){0.0f, 0.0f, 0.0f},
      .up = (Vector3){0.0f, 1.0f, 0.0f},
      .fovy = 45.0f,
      .projection = CAMERA_PERSPECTIVE,
  };
}

// agent: composer-2.5 | 2026-07-28 | render from js view registry | 9b1eee
static void mod_render_update_camera(ModRenderCtx *ctx) {
  mod_scene_runtime_use_view();
  const NgSceneViewMeta *view = mod_scene_assets_view();
  if (!view || !view->valid) {
    return;
  }
  ctx->camera.fovy = view->cam_fovy;
  ctx->camera.projection = CAMERA_PERSPECTIVE;
  if (view->camera_mode == NG_SCENE_CAM_ORBIT) {
    const float client_t = (float)GetTime();
    const float radius = view->orbit_radius;
    ctx->camera.position.x = sinf(client_t * view->orbit_speed) * radius;
    ctx->camera.position.z = cosf(client_t * view->orbit_speed) * radius;
    ctx->camera.position.y = view->orbit_height + sinf(client_t * view->orbit_speed * 0.5f) * 0.5f;
    ctx->camera.target =
        (Vector3){view->cam_target[0], view->cam_target[1], view->cam_target[2]};
  } else {
    ctx->camera.position =
        (Vector3){view->cam_pos[0], view->cam_pos[1], view->cam_pos[2]};
    ctx->camera.target =
        (Vector3){view->cam_target[0], view->cam_target[1], view->cam_target[2]};
  }
}

static float mod_render_lerp(float a, float b, float t) { return a + (b - a) * t; }

// agent: composer-2.5 | 2026-07-29 | overlay label view authority | 6d7863
// agent: composer-2.5 | 2026-07-30 | overlay connecting status | 0d072a
static bool mod_render_remote_connecting(char *out, size_t cap) {
#if defined(NG_HAS_EMBEDDED)
  char up_host[64] = {0};
  uint16_t up_port = 0;
  mod_net_upstream_endpoint(up_host, sizeof(up_host), &up_port);
  if (up_host[0] == '\0' || up_port == 0) {
    return false;
  }
  if (mod_scene_view_is_loaded()) {
    return false;
  }
  if (out && cap > 0) {
    snprintf(out, cap, "connecting at %s:%u (%.1fs)", up_host, up_port,
             mod_net_connect_elapsed());
  }
  return true;
#else
  (void)out;
  (void)cap;
  return false;
#endif
}

static const char *mod_render_authoritative_label(ModRenderCtx *ctx) {
  mod_scene_runtime_use_view();
  const char *view_id = mod_scene_view_current_id();
  if (mod_scene_view_is_loaded() && view_id && view_id[0] != '\0') {
    strncpy(ctx->scene_label, view_id, sizeof(ctx->scene_label) - 1);
    ctx->scene_label[sizeof(ctx->scene_label) - 1] = '\0';
    return ctx->scene_label;
  }
  return ctx->scene_label[0] != '\0' ? ctx->scene_label : "?";
}

static void mod_render_draw_overlay(const char *label, int y) {
  char connecting[128];
  if (mod_render_remote_connecting(connecting, sizeof(connecting))) {
    DrawText(connecting, 10, y, 20, RAYWHITE);
  } else {
    DrawText(TextFormat("scene: %s", label ? label : "?"), 10, y, 20, RAYWHITE);
  }
  const char *banner = mod_scene_native_banner();
  if (banner) {
    DrawText(banner, 10, y + 24, 18, YELLOW);
  }
}

static void mod_render_set_material_uniforms(const RenderAsset *a) {
  if (a->shader.loc_tint >= 0) {
    const float tint[3] = {(float)a->tint.r / 255.0f, (float)a->tint.g / 255.0f,
                           (float)a->tint.b / 255.0f};
    SetShaderValue(a->shader.handle, a->shader.loc_tint, tint, SHADER_UNIFORM_VEC3);
  }
  if (a->shader.loc_glow >= 0) {
    const float glow[3] = {(float)a->glow.r / 255.0f, (float)a->glow.g / 255.0f,
                           (float)a->glow.b / 255.0f};
    SetShaderValue(a->shader.handle, a->shader.loc_glow, glow, SHADER_UNIFORM_VEC3);
  }
  if (a->shader.loc_roughness >= 0) {
    SetShaderValue(a->shader.handle, a->shader.loc_roughness, &a->roughness, SHADER_UNIFORM_FLOAT);
  }
  if (a->shader.loc_metalness >= 0) {
    SetShaderValue(a->shader.handle, a->shader.loc_metalness, &a->metalness, SHADER_UNIFORM_FLOAT);
  }
}

static void mod_render_set_gbuf_uniforms(ModRenderCtx *ctx, const RenderAsset *a, int mode) {
  NgShader *sh = &ctx->gbuf_shader;
  ng_shader_set_common(sh, (float)GetTime());
  if (sh->loc_tint >= 0) {
    const float tint[3] = {(float)a->tint.r / 255.0f, (float)a->tint.g / 255.0f,
                           (float)a->tint.b / 255.0f};
    SetShaderValue(sh->handle, sh->loc_tint, tint, SHADER_UNIFORM_VEC3);
  }
  if (sh->loc_glow >= 0) {
    const float glow[3] = {(float)a->glow.r / 255.0f, (float)a->glow.g / 255.0f,
                           (float)a->glow.b / 255.0f};
    SetShaderValue(sh->handle, sh->loc_glow, glow, SHADER_UNIFORM_VEC3);
  }
  if (sh->loc_roughness >= 0) {
    SetShaderValue(sh->handle, sh->loc_roughness, &a->roughness, SHADER_UNIFORM_FLOAT);
  }
  if (sh->loc_metalness >= 0) {
    SetShaderValue(sh->handle, sh->loc_metalness, &a->metalness, SHADER_UNIFORM_FLOAT);
  }
  if (sh->loc_gbuf_mode >= 0) {
    SetShaderValue(sh->handle, sh->loc_gbuf_mode, &mode, SHADER_UNIFORM_INT);
  }
  if (sh->loc_cam_pos >= 0) {
    const float cam[3] = {ctx->camera.position.x, ctx->camera.position.y, ctx->camera.position.z};
    SetShaderValue(sh->handle, sh->loc_cam_pos, cam, SHADER_UNIFORM_VEC3);
  }
  // agent: composer-2.5 | 2026-08-10 | wire ws origin gbuf world | 4bc5ef
  // agent: composer-2.5 | 2026-08-10 | B1 dual clip near far RTs | d8ac7a
  {
    const NgRcWsClip *far = &ctx->ws_clip[NG_RC_WS_CLIP_FAR];
    int loc = GetShaderLocation(sh->handle, "ng_ws_origin");
    if (loc >= 0) {
      SetShaderValue(sh->handle, loc, far->origin, SHADER_UNIFORM_VEC3);
    }
    loc = GetShaderLocation(sh->handle, "ng_ws_size");
    if (loc >= 0) {
      SetShaderValue(sh->handle, loc, far->size, SHADER_UNIFORM_VEC3);
    }
  }
}

static void mod_render_unload_gbuf(ModRenderCtx *ctx) {
  if (ctx->gbuf_ready) {
    UnloadRenderTexture(ctx->rt_albedo);
    UnloadRenderTexture(ctx->rt_normal);
    UnloadRenderTexture(ctx->rt_glow);
    UnloadRenderTexture(ctx->rt_depth);
    UnloadRenderTexture(ctx->rt_prim_id);
    UnloadRenderTexture(ctx->rt_probe_id);
    ctx->gbuf_ready = false;
    ctx->gbuf_w = 0;
    ctx->gbuf_h = 0;
  }
  if (ctx->gbuf_shader_ready) {
    ng_shader_unload(&ctx->gbuf_shader);
    ctx->gbuf_shader_ready = false;
  }
}

static void mod_render_unload_rc(ModRenderCtx *ctx) {
  if (ctx->rc_rt_ready) {
    for (int i = 0; i < NG_RC_CASCADES_MAX; i++) {
      UnloadRenderTexture(ctx->rt_cascade[i]);
    }
    UnloadRenderTexture(ctx->rt_merge);
    UnloadRenderTexture(ctx->rt_ss_irr);
    ctx->rc_rt_ready = false;
    ctx->rc_w = 0;
    ctx->rc_h = 0;
  }
  if (ctx->ws_rt_ready) {
    // agent: composer-2.5 | 2026-08-10 | B1 dual clip near far RTs | d8ac7a
    for (int v = 0; v < NG_RC_WS_CLIP_COUNT; v++) {
      NgRcWsClip *clip = &ctx->ws_clip[v];
      for (int i = 0; i < NG_RC_CASCADES_MAX; i++) {
        UnloadRenderTexture(clip->casc[i]);
      }
      UnloadRenderTexture(clip->stamp);
      UnloadRenderTexture(clip->stamp2);
      UnloadRenderTexture(clip->sh);
    }
    UnloadRenderTexture(ctx->rt_ws[0]);
    UnloadRenderTexture(ctx->rt_ws[1]);
    ctx->ws_rt_ready = false;
    ctx->ws_probe_n = 0;
    ctx->ws_dirs = 0;
  }
  if (ctx->vox_rt_ready) {
    UnloadRenderTexture(ctx->rt_vox);
    ctx->vox_rt_ready = false;
  }
  ng_rc_ws_shutdown(&ctx->ws_cpu);
  if (ctx->rc_fill.ready) {
    ng_shader_unload(&ctx->rc_fill.sh);
    ctx->rc_fill.ready = false;
  }
  if (ctx->rc_merge.ready) {
    ng_shader_unload(&ctx->rc_merge.sh);
    ctx->rc_merge.ready = false;
  }
  if (ctx->rc_resolve.ready) {
    ng_shader_unload(&ctx->rc_resolve.sh);
    ctx->rc_resolve.ready = false;
  }
  if (ctx->rc_compose.ready) {
    ng_shader_unload(&ctx->rc_compose.sh);
    ctx->rc_compose.ready = false;
  }
  if (ctx->rc_ws_butter.ready) {
    ng_shader_unload(&ctx->rc_ws_butter.sh);
    ctx->rc_ws_butter.ready = false;
  }
  if (ctx->rc_ws_fill.ready) {
    ng_shader_unload(&ctx->rc_ws_fill.sh);
    ctx->rc_ws_fill.ready = false;
  }
  if (ctx->rc_ws_merge.ready) {
    ng_shader_unload(&ctx->rc_ws_merge.sh);
    ctx->rc_ws_merge.ready = false;
  }
  if (ctx->rc_ws_sh_encode.ready) {
    ng_shader_unload(&ctx->rc_ws_sh_encode.sh);
    ctx->rc_ws_sh_encode.ready = false;
  }
  if (ctx->rc_ws_resolve.ready) {
    ng_shader_unload(&ctx->rc_ws_resolve.sh);
    ctx->rc_ws_resolve.ready = false;
  }
  if (ctx->rc_ws_vox_stamp.ready) {
    ng_shader_unload(&ctx->rc_ws_vox_stamp.sh);
    ctx->rc_ws_vox_stamp.ready = false;
  }
  if (ctx->rc_ws_view.ready) {
    ng_shader_unload(&ctx->rc_ws_view.sh);
    ctx->rc_ws_view.ready = false;
  }
  // agent: composer-2.5 | 2026-08-10 | debug probes grid atlas wire | 49402d
  if (ctx->rc_ws_debug.ready) {
    ng_shader_unload(&ctx->rc_ws_debug.sh);
    ctx->rc_ws_debug.ready = false;
  }
  if (ctx->rc_ws_id.ready) {
    ng_shader_unload(&ctx->rc_ws_id.sh);
    ctx->rc_ws_id.ready = false;
  }
  // agent: composer-2.5 | 2026-08-11 | B64 wire GPU prio passes | f7b538
  if (ctx->rc_ws_prio.ready) {
    ng_shader_unload(&ctx->rc_ws_prio.sh);
    ctx->rc_ws_prio.ready = false;
  }
  if (ctx->rc_ws_prio_reduce.ready) {
    ng_shader_unload(&ctx->rc_ws_prio_reduce.sh);
    ctx->rc_ws_prio_reduce.ready = false;
  }
  if (ctx->ws_prio_gpu_ready) {
    UnloadRenderTexture(ctx->rt_ws_op);
    ctx->ws_prio_gpu_ready = false;
  }
  // agent: composer-2.5 | 2026-08-13 | CPU cull tick upload vis | 4d378d
  if (ctx->ws_cull_ready) {
    UnloadRenderTexture(ctx->rt_prim_vis);
    UnloadRenderTexture(ctx->rt_prim_vis_prev);
    ctx->ws_cull_ready = false;
  }
  ctx->ws_frustum_valid = false;
  if (ctx->rc_ws_probe.ready) {
    ng_shader_unload(&ctx->rc_ws_probe.sh);
    ctx->rc_ws_probe.ready = false;
  }
  if (ctx->rc_ws_probe_cover.ready) {
    ng_shader_unload(&ctx->rc_ws_probe_cover.sh);
    ctx->rc_ws_probe_cover.ready = false;
  }
  if (ctx->rc_ws_probe_apply.ready) {
    ng_shader_unload(&ctx->rc_ws_probe_apply.sh);
    ctx->rc_ws_probe_apply.ready = false;
  }
  if (ctx->rc_ws_probe_gen.ready) {
    ng_shader_unload(&ctx->rc_ws_probe_gen.sh);
    ctx->rc_ws_probe_gen.ready = false;
  }
  if (ctx->rc_ws_probe_union.ready) {
    ng_shader_unload(&ctx->rc_ws_probe_union.sh);
    ctx->rc_ws_probe_union.ready = false;
  }
  if (ctx->rc_ws_probe_release.ready) {
    ng_shader_unload(&ctx->rc_ws_probe_release.sh);
    ctx->rc_ws_probe_release.ready = false;
  }
  if (ctx->rc_ws_probe_merge.ready) {
    ng_shader_unload(&ctx->rc_ws_probe_merge.sh);
    ctx->rc_ws_probe_merge.ready = false;
  }
  if (ctx->rc_ws_probe_steal.ready) {
    ng_shader_unload(&ctx->rc_ws_probe_steal.sh);
    ctx->rc_ws_probe_steal.ready = false;
  }
  if (ctx->rc_ws_probe_stats.ready) {
    ng_shader_unload(&ctx->rc_ws_probe_stats.sh);
    ctx->rc_ws_probe_stats.ready = false;
  }
  // agent: grok-4.6 | 2026-08-12 | persist probe tick GPU | cabbaa
  if (ctx->rc_ws_probe_unmet.ready) {
    ng_shader_unload(&ctx->rc_ws_probe_unmet.sh);
    ctx->rc_ws_probe_unmet.ready = false;
  }
  if (ctx->rc_ws_probe_relax.ready) {
    ng_shader_unload(&ctx->rc_ws_probe_relax.sh);
    ctx->rc_ws_probe_relax.ready = false;
  }
  if (ctx->ws_probe_ready) {
    UnloadRenderTexture(ctx->rt_probe_work);
    UnloadRenderTexture(ctx->rt_probe_keep);
    UnloadRenderTexture(ctx->rt_probe_slots);
    UnloadRenderTexture(ctx->rt_probe_slots_b);
    UnloadRenderTexture(ctx->rt_probe_meta);
    UnloadRenderTexture(ctx->rt_probe_hash);
    UnloadRenderTexture(ctx->rt_probe_union);
    UnloadRenderTexture(ctx->rt_probe_stats);
    UnloadRenderTexture(ctx->rt_probe_unmet);
    ctx->ws_probe_ready = false;
    ctx->probe_gpu_valid = false;
  }
}

// agent: composer-2.5 | 2026-08-09 | present RT for render.scale | 2c679e
static void mod_render_unload_present(ModRenderCtx *ctx) {
  if (ctx->present_ready) {
    UnloadRenderTexture(ctx->rt_present);
    ctx->present_ready = false;
    ctx->present_w = 0;
    ctx->present_h = 0;
  }
}

/** Ensure color RT at viewport × render_scale. */
static bool mod_render_ensure_present(ModRenderCtx *ctx) {
  int w = 0;
  int h = 0;
  mod_render_internal_size(ctx, &w, &h);
  if (w <= 0 || h <= 0) {
    return false;
  }
  if (ctx->present_ready && ctx->present_w == w && ctx->present_h == h) {
    return true;
  }
  mod_render_unload_present(ctx);
  ctx->rt_present = LoadRenderTexture(w, h);
  ctx->present_w = w;
  ctx->present_h = h;
  ctx->present_ready = true;
  return true;
}

/** Blit scaled present buffer to the window (point filter when downscaled). */
static void mod_render_present_to_screen(ModRenderCtx *ctx) {
  if (!ctx->present_ready) {
    return;
  }
  const TextureFilter filt =
      (ctx->render_scale < 0.999f) ? TEXTURE_FILTER_POINT : TEXTURE_FILTER_BILINEAR;
  SetTextureFilter(ctx->rt_present.texture, filt);
  const Rectangle src = {0.0f, 0.0f, (float)ctx->rt_present.texture.width,
                         -(float)ctx->rt_present.texture.height};
  const Rectangle dst = {0.0f, 0.0f, (float)GetScreenWidth(), (float)GetScreenHeight()};
  DrawTexturePro(ctx->rt_present.texture, src, dst, (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
}

static bool mod_render_load_rc_pass(NgRcPassShader *pass, const char *fs) {
  pass->sh = ng_shader_load(NG_RES_ROOT "shaders/fullscreen.vs", fs);
  if (pass->sh.handle.id == 0) {
    return false;
  }
  pass->loc_tex_depth = GetShaderLocation(pass->sh.handle, "tex_depth");
  pass->loc_tex_albedo = GetShaderLocation(pass->sh.handle, "tex_albedo");
  pass->loc_tex_glow = GetShaderLocation(pass->sh.handle, "tex_glow");
  pass->loc_tex_normal = GetShaderLocation(pass->sh.handle, "tex_normal");
  pass->loc_tex_irradiance = GetShaderLocation(pass->sh.handle, "tex_irradiance");
  pass->loc_tex_irradiance_ws = GetShaderLocation(pass->sh.handle, "tex_irradiance_ws");
  pass->loc_tex_irradiance_ss = GetShaderLocation(pass->sh.handle, "tex_irradiance_ss");
  pass->loc_tex_self = GetShaderLocation(pass->sh.handle, "tex_self");
  pass->loc_tex_parent = GetShaderLocation(pass->sh.handle, "tex_parent");
  pass->loc_tex_prev = GetShaderLocation(pass->sh.handle, "tex_prev");
  pass->loc_tex_curr = GetShaderLocation(pass->sh.handle, "tex_curr");
  pass->loc_tex_atlas = GetShaderLocation(pass->sh.handle, "tex_atlas");
  pass->loc_tex_stamp = GetShaderLocation(pass->sh.handle, "tex_stamp");
  pass->loc_sky = GetShaderLocation(pass->sh.handle, "ng_sky");
  pass->loc_gi_strength = GetShaderLocation(pass->sh.handle, "ng_gi_strength");
  pass->loc_ws_weight = GetShaderLocation(pass->sh.handle, "ng_ws_weight");
  pass->loc_ss_weight = GetShaderLocation(pass->sh.handle, "ng_ss_weight");
  pass->loc_dir_count = GetShaderLocation(pass->sh.handle, "ng_dir_count");
  pass->loc_dir_side = GetShaderLocation(pass->sh.handle, "ng_dir_side");
  pass->loc_spacing = GetShaderLocation(pass->sh.handle, "ng_spacing");
  pass->loc_interval0 = GetShaderLocation(pass->sh.handle, "ng_interval0");
  pass->loc_interval1 = GetShaderLocation(pass->sh.handle, "ng_interval1");
  pass->loc_max_steps = GetShaderLocation(pass->sh.handle, "ng_max_steps");
  pass->loc_merge_weight = GetShaderLocation(pass->sh.handle, "ng_merge_weight");
  pass->loc_butter = GetShaderLocation(pass->sh.handle, "ng_butter");
  pass->loc_texel = GetShaderLocation(pass->sh.handle, "ng_texel");
  pass->loc_spread = GetShaderLocation(pass->sh.handle, "ng_spread");
  pass->loc_parent_texel = GetShaderLocation(pass->sh.handle, "ng_parent_texel");
  pass->loc_cascade_res = GetShaderLocation(pass->sh.handle, "ng_cascade_res");
  pass->loc_probes = GetShaderLocation(pass->sh.handle, "ng_probes");
  pass->loc_parent_probes = GetShaderLocation(pass->sh.handle, "ng_parent_probes");
  pass->loc_parent_res = GetShaderLocation(pass->sh.handle, "ng_parent_res");
  pass->loc_parent_dir_side = GetShaderLocation(pass->sh.handle, "ng_parent_dir_side");
  pass->loc_cam_pos = GetShaderLocation(pass->sh.handle, "ng_cam_pos");
  pass->loc_cam_forward = GetShaderLocation(pass->sh.handle, "ng_cam_forward");
  pass->loc_cam_right = GetShaderLocation(pass->sh.handle, "ng_cam_right");
  pass->loc_cam_up = GetShaderLocation(pass->sh.handle, "ng_cam_up");
  pass->loc_tan_half_fov = GetShaderLocation(pass->sh.handle, "ng_tan_half_fov");
  pass->loc_aspect = GetShaderLocation(pass->sh.handle, "ng_aspect");
  pass->loc_ws_origin = GetShaderLocation(pass->sh.handle, "ng_ws_origin");
  pass->loc_ws_size = GetShaderLocation(pass->sh.handle, "ng_ws_size");
  pass->loc_probe_res = GetShaderLocation(pass->sh.handle, "ng_probe_res");
  pass->ready = true;
  return true;
}

/** Quality → cascades / res shift / dir_side^2 dirs / steps. */
static void mod_render_rc_cost(int quality, int *out_cascades, int *out_shift,
                               int dir_side[NG_RC_CASCADES_MAX], int *out_steps) {
  // agent: composer-2.5 | 2026-08-10 | RC cost shift dirs cut | d9bceb
  int q = quality;
  if (q < 0) {
    q = 0;
  } else if (q > 4) {
    q = 4;
  }
  static const int k_cascades[5] = {1, 2, 3, 3, 3};
  /* Half-res cascades by default; full-res only at high quality. */
  static const int k_shift[5] = {1, 1, 1, 0, 0};
  static const int k_steps[5] = {2, 3, 4, 6, 8};
  /* Fewer dirs: side 2/2/4 → 4/4/16 (was up to 64). */
  static const int k_side[5][NG_RC_CASCADES_MAX] = {
      {2, 0, 0}, {2, 2, 0}, {2, 2, 4}, {2, 4, 4}, {2, 4, 8},
  };
  *out_cascades = k_cascades[q];
  *out_shift = k_shift[q];
  *out_steps = k_steps[q];
  for (int i = 0; i < NG_RC_CASCADES_MAX; i++) {
    dir_side[i] = k_side[q][i];
  }
}

/** Packed atlas: atlas = probes * dir_side (direction-first tiles). */
static void mod_render_rc_casc_dim(int base_w, int base_h, int c, int dir_side, int *out_px,
                                   int *out_py, int *out_aw, int *out_ah, int *out_spacing) {
  int spacing = 1 << c;
  int px = base_w / spacing;
  int py = base_h / spacing;
  if (px < 1) {
    px = 1;
  }
  if (py < 1) {
    py = 1;
  }
  if (dir_side < 1) {
    dir_side = 1;
  }
  *out_px = px;
  *out_py = py;
  *out_spacing = spacing;
  *out_aw = px * dir_side;
  *out_ah = py * dir_side;
}

static bool mod_render_ensure_rc(ModRenderCtx *ctx) {
  int w = 0;
  int h = 0;
  mod_render_internal_size(ctx, &w, &h);
  if (w <= 0 || h <= 0) {
    return false;
  }
  if (!ctx->rc_fill.ready &&
      !mod_render_load_rc_pass(&ctx->rc_fill, NG_RES_ROOT "shaders/rc_cascade_fill.fs")) {
    return false;
  }
  if (!ctx->rc_merge.ready &&
      !mod_render_load_rc_pass(&ctx->rc_merge, NG_RES_ROOT "shaders/rc_cascade_merge.fs")) {
    return false;
  }
  if (!ctx->rc_resolve.ready &&
      !mod_render_load_rc_pass(&ctx->rc_resolve, NG_RES_ROOT "shaders/rc_ss_resolve.fs")) {
    return false;
  }
  if (!ctx->rc_compose.ready &&
      !mod_render_load_rc_pass(&ctx->rc_compose, NG_RES_ROOT "shaders/rc_compose.fs")) {
    return false;
  }
  if (!ctx->rc_ws_butter.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_butter, NG_RES_ROOT "shaders/rc_ws_butter.fs")) {
    return false;
  }
  if (!ctx->rc_ws_fill.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_fill, NG_RES_ROOT "shaders/rc_ws_fill.fs")) {
    return false;
  }
  if (!ctx->rc_ws_merge.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_merge, NG_RES_ROOT "shaders/rc_ws_merge.fs")) {
    return false;
  }
  if (!ctx->rc_ws_sh_encode.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_sh_encode, NG_RES_ROOT "shaders/rc_ws_sh_encode.fs")) {
    return false;
  }
  if (!ctx->rc_ws_resolve.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_resolve, NG_RES_ROOT "shaders/rc_ws_resolve.fs")) {
    return false;
  }
  /* rc_ws_vox_stamp demoted with dense vox (6.2.4). */
  if (!ctx->rc_ws_view.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_view, NG_RES_ROOT "shaders/rc_ws_view.fs")) {
    return false;
  }
  // agent: composer-2.5 | 2026-08-10 | debug probes grid atlas wire | 49402d
  if (!ctx->rc_ws_debug.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_debug, NG_RES_ROOT "shaders/rc_ws_debug.fs")) {
    return false;
  }
  if (!ctx->rc_ws_id.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_id, NG_RES_ROOT "shaders/rc_ws_id.fs")) {
    return false;
  }
  // agent: composer-2.5 | 2026-08-11 | B64 wire GPU prio passes | f7b538
  /* Prio score/reduce: optional — CPU select from prio_rgba is default (0 download). */
  if (!ctx->rc_ws_prio.ready) {
    (void)mod_render_load_rc_pass(&ctx->rc_ws_prio, NG_RES_ROOT "shaders/rc_ws_prio.fs");
  }
  if (!ctx->rc_ws_prio_reduce.ready) {
    (void)mod_render_load_rc_pass(&ctx->rc_ws_prio_reduce, NG_RES_ROOT "shaders/rc_ws_prio_reduce.fs");
  }
  if (!ctx->ws_prio_gpu_ready && ctx->rc_ws_prio_reduce.ready) {
    ctx->rt_ws_op = LoadRenderTexture(1, 1);
    if (ctx->rt_ws_op.id != 0) {
      SetTextureFilter(ctx->rt_ws_op.texture, TEXTURE_FILTER_POINT);
      ctx->ws_prio_gpu_ready = true;
    }
  }
  // agent: composer-2.5 | 2026-08-13 | CPU cull tick upload vis | 4d378d
  if (!ctx->ws_cull_ready) {
    ctx->rt_prim_vis = LoadRenderTexture(NG_RC_WS_PRIM_MAX, 2);
    ctx->rt_prim_vis_prev = LoadRenderTexture(NG_RC_WS_PRIM_MAX, 2);
    if (ctx->rt_prim_vis.id == 0 || ctx->rt_prim_vis_prev.id == 0) {
      return false;
    }
    SetTextureFilter(ctx->rt_prim_vis.texture, TEXTURE_FILTER_POINT);
    SetTextureFilter(ctx->rt_prim_vis_prev.texture, TEXTURE_FILTER_POINT);
    SetTextureWrap(ctx->rt_prim_vis.texture, TEXTURE_WRAP_CLAMP);
    SetTextureWrap(ctx->rt_prim_vis_prev.texture, TEXTURE_WRAP_CLAMP);
    BeginTextureMode(ctx->rt_prim_vis_prev);
    ClearBackground(BLACK);
    EndTextureMode();
    ctx->ws_cull_ready = true;
  }
  if (!ctx->rc_ws_probe.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_probe, NG_RES_ROOT "shaders/rc_ws_probe.fs")) {
    return false;
  }
  if (!ctx->rc_ws_probe_cover.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_probe_cover, NG_RES_ROOT "shaders/rc_ws_probe_cover.fs")) {
    return false;
  }
  if (!ctx->rc_ws_probe_apply.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_probe_apply, NG_RES_ROOT "shaders/rc_ws_probe_apply.fs")) {
    return false;
  }
  if (!ctx->rc_ws_probe_gen.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_probe_gen, NG_RES_ROOT "shaders/rc_ws_probe_gen.fs")) {
    return false;
  }
  if (!ctx->rc_ws_probe_union.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_probe_union, NG_RES_ROOT "shaders/rc_ws_probe_union.fs")) {
    return false;
  }
  if (!ctx->rc_ws_probe_release.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_probe_release,
                               NG_RES_ROOT "shaders/rc_ws_probe_release.fs")) {
    return false;
  }
  if (!ctx->rc_ws_probe_merge.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_probe_merge, NG_RES_ROOT "shaders/rc_ws_probe_merge.fs")) {
    return false;
  }
  if (!ctx->rc_ws_probe_steal.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_probe_steal, NG_RES_ROOT "shaders/rc_ws_probe_steal.fs")) {
    return false;
  }
  // agent: grok-4.6 | 2026-08-12 | persist probe tick GPU | cabbaa
  if (!ctx->rc_ws_probe_unmet.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_probe_unmet, NG_RES_ROOT "shaders/rc_ws_probe_unmet.fs")) {
    return false;
  }
  if (!ctx->rc_ws_probe_relax.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_probe_relax, NG_RES_ROOT "shaders/rc_ws_probe_relax.fs")) {
    return false;
  }
  if (!ctx->rc_ws_probe_stats.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_probe_stats, NG_RES_ROOT "shaders/rc_ws_probe_stats.fs")) {
    return false;
  }
  if (!ctx->ws_probe_ready) {
    // agent: composer-2.5 | 2026-08-12 | GPU probe residency RTs | 4b745d
    // agent: composer-2.5 | 2026-08-12 | slots ping-pong for split | f33dd4
    // agent: composer-2.5 | 2026-08-12 | probe incremental residency tick | 5af26a
    // agent: composer-2.5 | 2026-08-12 | lazy stochastic split steal | 1c5864
    ctx->rt_probe_work = mod_render_load_rt_rgba32f(NG_RC_WS_PROBE_WORK_MAX, 1);
    ctx->rt_probe_keep = LoadRenderTexture(NG_RC_WS_PROBE_WORK_MAX, 2);
    ctx->rt_probe_slots = mod_render_load_rt_rgba32f(NG_RC_WS_SLOT_MAX, 1);
    ctx->rt_probe_slots_b = mod_render_load_rt_rgba32f(NG_RC_WS_SLOT_MAX, 1);
    ctx->rt_probe_meta = mod_render_load_rt_rgba32f(1, NG_RC_WS_SLOT_MAX);
    ctx->rt_probe_hash = mod_render_load_rt_rgba32f(NG_RC_WS_HASH_MAX, 1);
    ctx->rt_probe_union = mod_render_load_rt_rgba32f(2, 1);
    ctx->rt_probe_stats = mod_render_load_rt_rgba32f(1, 1);
    ctx->rt_probe_unmet = mod_render_load_rt_rgba32f(1, 1);
    if (ctx->rt_probe_work.id == 0 || ctx->rt_probe_keep.id == 0 || ctx->rt_probe_slots.id == 0 ||
        ctx->rt_probe_slots_b.id == 0 || ctx->rt_probe_meta.id == 0 || ctx->rt_probe_hash.id == 0 ||
        ctx->rt_probe_union.id == 0 || ctx->rt_probe_stats.id == 0 || ctx->rt_probe_unmet.id == 0) {
      return false;
    }
    SetTextureFilter(ctx->rt_probe_keep.texture, TEXTURE_FILTER_POINT);
    SetTextureWrap(ctx->rt_probe_keep.texture, TEXTURE_WRAP_CLAMP);
    ctx->ws_probe_ready = true;
    ctx->probe_gpu_valid = false;
    ctx->probe_frame = 0;
  }
  if (!ng_rc_ws_ensure(&ctx->ws_cpu, ctx->rc_quality)) {
    return false;
  }
#if !NG_RC_WS_GI_OFFLINE
  // agent: composer-2.5 | 2026-08-10 | B1 dual clip near far RTs | d8ac7a
  {
    const NgRcWsClip *far = &ctx->ws_clip[NG_RC_WS_CLIP_FAR];
    ctx->ws_cpu.origin[0] = far->origin[0];
    ctx->ws_cpu.origin[1] = far->origin[1];
    ctx->ws_cpu.origin[2] = far->origin[2];
    ctx->ws_cpu.size[0] = far->size[0];
    ctx->ws_cpu.size[1] = far->size[1];
    ctx->ws_cpu.size[2] = far->size[2];
  }
#endif

  int cascades = 3;
  int shift = 0;
  int dir_side[NG_RC_CASCADES_MAX];
  int steps = 6;
  mod_render_rc_cost(ctx->rc_quality, &cascades, &shift, dir_side, &steps);
  (void)steps;
  const int bw = w >> shift;
  const int bh = h >> shift;
  const int base_w = bw > 0 ? bw : 1;
  const int base_h = bh > 0 ? bh : 1;
  const int probe_n = ctx->ws_cpu.probe_n > 0 ? ctx->ws_cpu.probe_n : 12;
  const int ws_dirs = ctx->ws_cpu.dirs > 0 ? ctx->ws_cpu.dirs : 8;

  int need_rebuild = !ctx->rc_rt_ready || ctx->rc_w != base_w || ctx->rc_h != base_h ||
                     ctx->rc_cascades != cascades;
  if (!need_rebuild) {
    for (int i = 0; i < cascades; i++) {
      if (ctx->rc_dir_side[i] != dir_side[i]) {
        need_rebuild = 1;
        break;
      }
    }
  }

  const int ws_ok = ctx->ws_rt_ready && ctx->ws_probe_n == probe_n && ctx->ws_dirs == ws_dirs &&
                    ctx->ws_irr_w == w && ctx->ws_irr_h == h;
  if (!need_rebuild && ws_ok) {
    return true;
  }
  if (need_rebuild && ctx->rc_rt_ready) {
    for (int i = 0; i < NG_RC_CASCADES_MAX; i++) {
      UnloadRenderTexture(ctx->rt_cascade[i]);
    }
    UnloadRenderTexture(ctx->rt_merge);
    UnloadRenderTexture(ctx->rt_ss_irr);
    ctx->rc_rt_ready = false;
  }
  if (!ctx->rc_rt_ready) {
    int max_aw = 1;
    int max_ah = 1;
    for (int i = 0; i < NG_RC_CASCADES_MAX; i++) {
      int px = 1;
      int py = 1;
      int aw = 1;
      int ah = 1;
      int sp = 1;
      const int ds = dir_side[i] > 0 ? dir_side[i] : 1;
      mod_render_rc_casc_dim(base_w, base_h, i, ds, &px, &py, &aw, &ah, &sp);
      ctx->rc_dir_side[i] = dir_side[i];
      ctx->rc_probes_x[i] = px;
      ctx->rc_probes_y[i] = py;
      ctx->rc_spacing[i] = sp;
      if (i < cascades) {
        ctx->rt_cascade[i] = LoadRenderTexture(aw, ah);
        SetTextureFilter(ctx->rt_cascade[i].texture, TEXTURE_FILTER_BILINEAR);
        if (aw > max_aw) {
          max_aw = aw;
        }
        if (ah > max_ah) {
          max_ah = ah;
        }
      } else {
        ctx->rt_cascade[i] = LoadRenderTexture(1, 1);
      }
    }
    ctx->rt_merge = LoadRenderTexture(max_aw, max_ah);
    SetTextureFilter(ctx->rt_merge.texture, TEXTURE_FILTER_BILINEAR);
    ctx->rt_ss_irr = LoadRenderTexture(base_w, base_h);
    SetTextureFilter(ctx->rt_ss_irr.texture, TEXTURE_FILTER_BILINEAR);
    ctx->rc_w = base_w;
    ctx->rc_h = base_h;
    ctx->rc_cascades = cascades;
    ctx->rc_rt_ready = true;
  }

  // agent: composer-2.5 | 2026-08-10 | true WS RC tick cascade chain | 82877c
  // agent: composer-2.5 | 2026-08-10 | B1 dual clip near far RTs | d8ac7a
  if (ctx->ws_rt_ready && !ws_ok) {
    for (int v = 0; v < NG_RC_WS_CLIP_COUNT; v++) {
      NgRcWsClip *clip = &ctx->ws_clip[v];
      for (int i = 0; i < NG_RC_CASCADES_MAX; i++) {
        UnloadRenderTexture(clip->casc[i]);
      }
      UnloadRenderTexture(clip->stamp);
      UnloadRenderTexture(clip->stamp2);
      UnloadRenderTexture(clip->sh);
    }
    UnloadRenderTexture(ctx->rt_ws[0]);
    UnloadRenderTexture(ctx->rt_ws[1]);
    ctx->ws_rt_ready = false;
  }
  if (!ctx->ws_rt_ready) {
    // agent: composer-2.5 | 2026-08-10 | B3 sparse tick seed fill | 4d7b85
    const int aw = ws_dirs;
    const int ah = probe_n;
    const int sh_w = 4;
    for (int v = 0; v < NG_RC_WS_CLIP_COUNT; v++) {
      NgRcWsClip *clip = &ctx->ws_clip[v];
      for (int i = 0; i < NG_RC_CASCADES_MAX; i++) {
        clip->casc[i] = LoadRenderTexture(aw, ah);
        SetTextureFilter(clip->casc[i].texture, TEXTURE_FILTER_POINT);
        BeginTextureMode(clip->casc[i]);
        ClearBackground(BLACK);
        EndTextureMode();
      }
      clip->stamp = LoadRenderTexture(aw, ah);
      SetTextureFilter(clip->stamp.texture, TEXTURE_FILTER_POINT);
      BeginTextureMode(clip->stamp);
      ClearBackground(BLACK);
      EndTextureMode();
      // agent: composer-2.5 | 2026-08-10 | merge pingpong not casc | 27d0fe
      clip->stamp2 = LoadRenderTexture(aw, ah);
      SetTextureFilter(clip->stamp2.texture, TEXTURE_FILTER_POINT);
      BeginTextureMode(clip->stamp2);
      ClearBackground(BLACK);
      EndTextureMode();
      clip->sh = LoadRenderTexture(sh_w, ah);
      SetTextureFilter(clip->sh.texture, TEXTURE_FILTER_POINT);
      BeginTextureMode(clip->sh);
      ClearBackground(BLACK);
      EndTextureMode();
    }
    ctx->rt_ws[0] = LoadRenderTexture(w, h);
    ctx->rt_ws[1] = LoadRenderTexture(w, h);
    SetTextureFilter(ctx->rt_ws[0].texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(ctx->rt_ws[1].texture, TEXTURE_FILTER_BILINEAR);
    // agent: composer-2.5 | 2026-08-11 | B6 kill seed readback tick | 7c8edc
    BeginTextureMode(ctx->rt_ws[0]);
    ClearBackground(BLACK);
    EndTextureMode();
    BeginTextureMode(ctx->rt_ws[1]);
    ClearBackground(BLACK);
    EndTextureMode();
    ctx->ws_probe_n = probe_n;
    ctx->ws_dirs = ws_dirs;
    ctx->ws_irr_w = w;
    ctx->ws_irr_h = h;
    ctx->ws_ping = 0;
    ctx->ws_rt_ready = true;
    ctx->ws_vox_scene_hash = 0;
  }
  return true;
}

static void mod_render_fs_draw(Texture2D carrier, int dest_w, int dest_h) {
  const Rectangle src = {0.0f, 0.0f, (float)carrier.width, -(float)carrier.height};
  const Rectangle dst = {0.0f, 0.0f, (float)dest_w, (float)dest_h};
  DrawTexturePro(carrier, src, dst, (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
}

/** WS volume atlas draw: identity UV (no raylib Y flip) — matches FragCoord fill. */
static void mod_render_ws_fs_draw(Texture2D carrier, int dest_w, int dest_h) {
  // agent: composer-2.5 | 2026-08-10 | WS blit identity no flip | b8e489
  const Rectangle src = {0.0f, 0.0f, (float)carrier.width, (float)carrier.height};
  const Rectangle dst = {0.0f, 0.0f, (float)dest_w, (float)dest_h};
  DrawTexturePro(carrier, src, dst, (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
}

static void mod_render_rc_fill_cascade(ModRenderCtx *ctx, int c, float t0, float t1, int max_steps) {
  NgRcPassShader *pass = &ctx->rc_fill;
  RenderTexture2D *dest = &ctx->rt_cascade[c];
  const float res[2] = {(float)ctx->rc_w, (float)ctx->rc_h};
  const float cres[2] = {(float)dest->texture.width, (float)dest->texture.height};
  const float probes[2] = {(float)ctx->rc_probes_x[c], (float)ctx->rc_probes_y[c]};
  const int dir_side = ctx->rc_dir_side[c] > 0 ? ctx->rc_dir_side[c] : 1;
  const int spacing = ctx->rc_spacing[c] > 0 ? ctx->rc_spacing[c] : 1;
  const float sky[3] = {NG_RC_SKY.x, NG_RC_SKY.y, NG_RC_SKY.z};
  BeginTextureMode(*dest);
  ClearBackground(BLACK);
  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  if (pass->sh.loc_resolution >= 0) {
    SetShaderValue(pass->sh.handle, pass->sh.loc_resolution, res, SHADER_UNIFORM_VEC2);
  }
  if (pass->loc_cascade_res >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_cascade_res, cres, SHADER_UNIFORM_VEC2);
  }
  if (pass->loc_probes >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_probes, probes, SHADER_UNIFORM_VEC2);
  }
  if (pass->loc_dir_side >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_dir_side, &dir_side, SHADER_UNIFORM_INT);
  }
  if (pass->loc_spacing >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_spacing, &spacing, SHADER_UNIFORM_INT);
  }
  if (pass->loc_interval0 >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_interval0, &t0, SHADER_UNIFORM_FLOAT);
  }
  if (pass->loc_interval1 >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_interval1, &t1, SHADER_UNIFORM_FLOAT);
  }
  if (pass->loc_max_steps >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_max_steps, &max_steps, SHADER_UNIFORM_INT);
  }
  if (pass->loc_sky >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_sky, sky, SHADER_UNIFORM_VEC3);
  }
  // agent: composer-2.5 | 2026-08-10 | SS dist from world uvw | 39bad7
  if (pass->loc_cam_pos >= 0) {
    const float cam[3] = {ctx->camera.position.x, ctx->camera.position.y, ctx->camera.position.z};
    SetShaderValue(pass->sh.handle, pass->loc_cam_pos, cam, SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_ws_origin >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_ws_origin, ctx->ws_clip[NG_RC_WS_CLIP_FAR].origin,
                   SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_ws_size >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_ws_size, ctx->ws_clip[NG_RC_WS_CLIP_FAR].size,
                   SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_tex_depth >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_depth, ctx->rt_depth.texture);
  }
  if (pass->loc_tex_albedo >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_albedo, ctx->rt_albedo.texture);
  }
  // agent: composer-2.5 | 2026-08-10 | SS fill no glow hits | 94de82
  mod_render_fs_draw(ctx->rt_depth.texture, dest->texture.width, dest->texture.height);
  EndShaderMode();
  EndTextureMode();
}

static void mod_render_rc_merge(ModRenderCtx *ctx, int child, int parent) {
  NgRcPassShader *pass = &ctx->rc_merge;
  const int dw = ctx->rt_cascade[child].texture.width;
  const int dh = ctx->rt_cascade[child].texture.height;
  const float cres[2] = {(float)dw, (float)dh};
  const float probes[2] = {(float)ctx->rc_probes_x[child], (float)ctx->rc_probes_y[child]};
  const float pprobes[2] = {(float)ctx->rc_probes_x[parent], (float)ctx->rc_probes_y[parent]};
  const float pres[2] = {(float)ctx->rt_cascade[parent].texture.width,
                         (float)ctx->rt_cascade[parent].texture.height};
  const int dir_side = ctx->rc_dir_side[child] > 0 ? ctx->rc_dir_side[child] : 1;
  const int p_side = ctx->rc_dir_side[parent] > 0 ? ctx->rc_dir_side[parent] : 1;
  BeginTextureMode(ctx->rt_merge);
  ClearBackground(BLACK);
  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  if (pass->loc_cascade_res >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_cascade_res, cres, SHADER_UNIFORM_VEC2);
  }
  if (pass->loc_probes >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_probes, probes, SHADER_UNIFORM_VEC2);
  }
  if (pass->loc_parent_probes >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_parent_probes, pprobes, SHADER_UNIFORM_VEC2);
  }
  if (pass->loc_parent_res >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_parent_res, pres, SHADER_UNIFORM_VEC2);
  }
  if (pass->loc_dir_side >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_dir_side, &dir_side, SHADER_UNIFORM_INT);
  }
  if (pass->loc_parent_dir_side >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_parent_dir_side, &p_side, SHADER_UNIFORM_INT);
  }
  if (pass->loc_tex_self >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_self, ctx->rt_cascade[child].texture);
  }
  if (pass->loc_tex_parent >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_parent, ctx->rt_cascade[parent].texture);
  }
  {
    const Rectangle src = {0.0f, 0.0f, (float)dw, -(float)dh};
    const Rectangle dst = {0.0f, 0.0f, (float)dw, (float)dh};
    DrawTexturePro(ctx->rt_cascade[child].texture, src, dst, (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
  }
  EndShaderMode();
  EndTextureMode();

  BeginTextureMode(ctx->rt_cascade[child]);
  ClearBackground(BLACK);
  {
    const Rectangle src = {0.0f, 0.0f, (float)dw, -(float)dh};
    const Rectangle dst = {0.0f, 0.0f, (float)dw, (float)dh};
    DrawTexturePro(ctx->rt_merge.texture, src, dst, (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
  }
  EndTextureMode();
}

/** Resolve packed c0 dirs → full-res SS irradiance. */
static void mod_render_rc_resolve(ModRenderCtx *ctx) {
  NgRcPassShader *pass = &ctx->rc_resolve;
  const float res[2] = {(float)ctx->rc_w, (float)ctx->rc_h};
  const float cres[2] = {(float)ctx->rt_cascade[0].texture.width,
                         (float)ctx->rt_cascade[0].texture.height};
  const float probes[2] = {(float)ctx->rc_probes_x[0], (float)ctx->rc_probes_y[0]};
  const int dir_side = ctx->rc_dir_side[0] > 0 ? ctx->rc_dir_side[0] : 1;
  const int spacing = ctx->rc_spacing[0] > 0 ? ctx->rc_spacing[0] : 1;
  BeginTextureMode(ctx->rt_ss_irr);
  ClearBackground(BLACK);
  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  if (pass->sh.loc_resolution >= 0) {
    SetShaderValue(pass->sh.handle, pass->sh.loc_resolution, res, SHADER_UNIFORM_VEC2);
  }
  if (pass->loc_cascade_res >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_cascade_res, cres, SHADER_UNIFORM_VEC2);
  }
  if (pass->loc_probes >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_probes, probes, SHADER_UNIFORM_VEC2);
  }
  if (pass->loc_dir_side >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_dir_side, &dir_side, SHADER_UNIFORM_INT);
  }
  if (pass->loc_spacing >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_spacing, &spacing, SHADER_UNIFORM_INT);
  }
  if (pass->loc_tex_depth >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_depth, ctx->rt_depth.texture);
  }
  if (pass->loc_tex_normal >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_normal, ctx->rt_normal.texture);
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_cascade");
    if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_cascade[0].texture);
    } else if (pass->loc_tex_self >= 0) {
      SetShaderValueTexture(pass->sh.handle, pass->loc_tex_self, ctx->rt_cascade[0].texture);
    }
  }
  mod_render_fs_draw(ctx->rt_depth.texture, ctx->rt_ss_irr.texture.width, ctx->rt_ss_irr.texture.height);
  EndShaderMode();
  EndTextureMode();
}

/** Compose Direct + gi*(ws·WS + ss·SS)*albedo + glow. */
static void mod_render_rc_compose(ModRenderCtx *ctx) {
  NgRcPassShader *pass = &ctx->rc_compose;
#if NG_RC_WS_GI_OFFLINE
  const float gi = 0.0f; /* foundation: ambient + direct only */
#else
  const float gi = ctx->gi_strength;
#endif
  const float ws_w = ctx->ws_weight;
  const float ss_w = ctx->ss_weight;
  const float sky[3] = {NG_RC_SKY.x, NG_RC_SKY.y, NG_RC_SKY.z};
  const float probe_res = (float)(ctx->ws_probe_n > 0 ? ctx->ws_probe_n : 12);
  // agent: composer-2.5 | 2026-08-10 | true WS RC tick cascade chain | 82877c
  const int ping = ctx->ws_ping & 1;
  // agent: composer-2.5 | 2026-08-10 | compose view use present size | 316071
  int pw = 0;
  int ph = 0;
  mod_render_internal_size(ctx, &pw, &ph);
  const float res[2] = {(float)pw, (float)ph};

  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  if (pass->sh.loc_resolution >= 0) {
    SetShaderValue(pass->sh.handle, pass->sh.loc_resolution, res, SHADER_UNIFORM_VEC2);
  }
  if (pass->loc_gi_strength >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_gi_strength, &gi, SHADER_UNIFORM_FLOAT);
  }
  if (pass->loc_ws_weight >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_ws_weight, &ws_w, SHADER_UNIFORM_FLOAT);
  }
  if (pass->loc_ss_weight >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_ss_weight, &ss_w, SHADER_UNIFORM_FLOAT);
  }
  if (pass->loc_sky >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_sky, sky, SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_ws_origin >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_ws_origin, ctx->ws_clip[NG_RC_WS_CLIP_FAR].origin,
                   SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_ws_size >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_ws_size, ctx->ws_clip[NG_RC_WS_CLIP_FAR].size,
                   SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_probe_res >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_probe_res, &probe_res, SHADER_UNIFORM_FLOAT);
  }
  // agent: composer-2.5 | 2026-08-10 | compose bind cam for specular | d19ab3
  if (pass->loc_cam_pos >= 0) {
    const float cam[3] = {ctx->camera.position.x, ctx->camera.position.y, ctx->camera.position.z};
    SetShaderValue(pass->sh.handle, pass->loc_cam_pos, cam, SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_tex_albedo >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_albedo, ctx->rt_albedo.texture);
  }
  if (pass->loc_tex_normal >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_normal, ctx->rt_normal.texture);
  }
  /* Glow before optional SS — unbound glow samples unit0 (white rect) → full white. */
  // agent: composer-2.5 | 2026-08-10 | compose glow bind order fix | 08a65e
  if (pass->loc_tex_glow >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_glow, ctx->rt_glow.texture);
  }
  if (pass->loc_tex_depth >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_depth, ctx->rt_depth.texture);
  }
  if (pass->loc_tex_irradiance_ws >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_irradiance_ws, ctx->rt_ws[ping].texture);
  }
  if (ss_w > 0.0f && pass->loc_tex_irradiance_ss >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_irradiance_ss, ctx->rt_ss_irr.texture);
  }
  /* Carrier = depth so unit0 is not white if a sampler misses a slot. */
  mod_render_fs_draw(ctx->rt_depth.texture, pw, ph);
  EndShaderMode();
}

/** Fill one WS cascade interval (sparse slots; dirty-only when requested). */
static void mod_render_rc_ws_casc_fill(ModRenderCtx *ctx, NgRcWsClip *clip, int c, float t0,
                                      float t1, int dirty_only) {
  // agent: composer-2.5 | 2026-08-10 | B3 fill skip clear dirty only | 93345e
  NgRcPassShader *pass = &ctx->rc_ws_fill;
  const NgRcWsClip *far = &ctx->ws_clip[NG_RC_WS_CLIP_FAR];
  const float sky[3] = {NG_RC_SKY.x, NG_RC_SKY.y, NG_RC_SKY.z};
  const float probe_res = (float)(ctx->ws_probe_n > 0 ? ctx->ws_probe_n : 12);
  const int dirs = ctx->ws_dirs > 0 ? ctx->ws_dirs : 8;
  const int steps = ctx->ws_cpu.steps > 0 ? ctx->ws_cpu.steps : 5;
  const int prim_count = ctx->ws_cpu.prim_count;
  const float dirty_f = dirty_only ? 1.0f : 0.0f;
  RenderTexture2D *dest = &clip->casc[c];
  BeginTextureMode(*dest);
  if (!dirty_only) {
    ClearBackground(BLACK);
  }
  /* a=0 miss must replace dst — else reused slots keep prior cell RGB. */
  // agent: composer-2.5 | 2026-08-10 | ws fill disable blend dirty | 466455
  rlDisableColorBlend();
  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  if (pass->loc_ws_origin >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_ws_origin, clip->origin, SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_ws_size >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_ws_size, clip->size, SHADER_UNIFORM_VEC3);
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_grid_origin");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, far->origin, SHADER_UNIFORM_VEC3);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_grid_size");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, far->size, SHADER_UNIFORM_VEC3);
    }
  }
  if (pass->loc_probe_res >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_probe_res, &probe_res, SHADER_UNIFORM_FLOAT);
  }
  if (pass->loc_dir_count >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_dir_count, &dirs, SHADER_UNIFORM_INT);
  }
  if (pass->loc_max_steps >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_max_steps, &steps, SHADER_UNIFORM_INT);
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_prim_count");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &prim_count, SHADER_UNIFORM_INT);
    }
  }
  if (pass->loc_interval0 >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_interval0, &t0, SHADER_UNIFORM_FLOAT);
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_t0");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &t0, SHADER_UNIFORM_FLOAT);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_t1");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &t1, SHADER_UNIFORM_FLOAT);
    }
  }
  if (pass->loc_interval1 >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_interval1, &t1, SHADER_UNIFORM_FLOAT);
  }
  if (pass->loc_sky >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_sky, sky, SHADER_UNIFORM_VEC3);
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_prim");
    if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc, ctx->ws_cpu.tex_prim);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_grid");
    if (loc >= 0 && ctx->ws_cpu.grid_tex_ready) {
      SetShaderValueTexture(pass->sh.handle, loc, ctx->ws_cpu.tex_grid);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_meta");
    if (loc >= 0 && ctx->ws_cpu.sparse_tex_ready) {
      SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_probe_meta.texture);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_fill_dirty_only");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &dirty_f, SHADER_UNIFORM_FLOAT);
    }
  }
  {
    const float grid_res = (float)NG_RC_WS_GRID_RES;
    int loc = GetShaderLocation(pass->sh.handle, "ng_grid_res");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &grid_res, SHADER_UNIFORM_FLOAT);
    }
  }
  mod_render_ws_fs_draw(ctx->ws_cpu.tex_prim, dest->texture.width, dest->texture.height);
  EndShaderMode();
  rlEnableColorBlend();
  EndTextureMode();
}

/** T-merge near + far cascade intervals → dest RT. */
static void mod_render_rc_ws_merge_to(ModRenderCtx *ctx, Texture2D near_tex, Texture2D far_tex,
                                     RenderTexture2D *dest) {
  NgRcPassShader *pass = &ctx->rc_ws_merge;
  BeginTextureMode(*dest);
  ClearBackground(BLACK);
  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_near");
    if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc, near_tex);
    } else if (pass->loc_tex_self >= 0) {
      SetShaderValueTexture(pass->sh.handle, pass->loc_tex_self, near_tex);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_far");
    if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc, far_tex);
    } else if (pass->loc_tex_parent >= 0) {
      SetShaderValueTexture(pass->sh.handle, pass->loc_tex_parent, far_tex);
    }
  }
  DrawRectangle(0, 0, dest->texture.width, dest->texture.height, WHITE);
  EndShaderMode();
  EndTextureMode();
}

/** Encode merged dir atlas → L1 SH for one clip. */
static void mod_render_rc_ws_sh_encode(ModRenderCtx *ctx, NgRcWsClip *clip, Texture2D merged) {
  NgRcPassShader *pass = &ctx->rc_ws_sh_encode;
  const float probe_res = (float)(ctx->ws_probe_n > 0 ? ctx->ws_probe_n : 12);
  const int dirs = ctx->ws_dirs > 0 ? ctx->ws_dirs : 8;
  const float cres[2] = {(float)merged.width, (float)merged.height};
  BeginTextureMode(clip->sh);
  ClearBackground(BLACK);
  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  if (pass->loc_probe_res >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_probe_res, &probe_res, SHADER_UNIFORM_FLOAT);
  }
  if (pass->loc_dir_count >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_dir_count, &dirs, SHADER_UNIFORM_INT);
  }
  if (pass->loc_cascade_res >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_cascade_res, cres, SHADER_UNIFORM_VEC2);
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_cascade");
    if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc, merged);
    }
  }
  DrawRectangle(0, 0, clip->sh.texture.width, clip->sh.texture.height, WHITE);
  EndShaderMode();
  EndTextureMode();
}

/** Fill+merge+SH; dirty_only skips clear and remarch of clean slots. */
static void mod_render_rc_ws_fill_volume(ModRenderCtx *ctx, NgRcWsClip *clip, int dirty_only) {
  // agent: composer-2.5 | 2026-08-10 | B3 fill skip clear dirty only | 93345e
  int nc = ctx->ws_cpu.cascades > 0 ? ctx->ws_cpu.cascades : 3;
  if (nc > NG_RC_CASCADES_MAX) {
    nc = NG_RC_CASCADES_MAX;
  }
  const float cell = NG_RC_WS_CELL;
  float t0 = cell * 0.55f;
  float t1 = cell * 1.45f;
  for (int c = 0; c < nc; c++) {
    mod_render_rc_ws_casc_fill(ctx, clip, c, t0, t1, dirty_only);
    t0 = t1;
    t1 *= 2.0f;
  }

  Texture2D merged = clip->casc[nc - 1].texture;
  int to_a = 1;
  for (int c = nc - 2; c >= 0; c--) {
    /* Ping-pong stamp/stamp2 only — casc[] stay pure interval fills for dirty-only. */
    RenderTexture2D *dest = to_a ? &clip->stamp : &clip->stamp2;
    mod_render_rc_ws_merge_to(ctx, clip->casc[c].texture, merged, dest);
    merged = dest->texture;
    to_a ^= 1;
  }
  mod_render_rc_ws_sh_encode(ctx, clip, merged);
}

/** Soft-nearest SH eval sparse hash → screen irr. */
static void mod_render_rc_ws_resolve(ModRenderCtx *ctx, int dest) {
  // agent: composer-2.5 | 2026-08-10 | B3 sparse tick seed fill | 4d7b85
  NgRcPassShader *pass = &ctx->rc_ws_resolve;
  const NgRcWsClip *far = &ctx->ws_clip[NG_RC_WS_CLIP_FAR];
  const float sky[3] = {NG_RC_SKY.x, NG_RC_SKY.y, NG_RC_SKY.z};
  const float sres[2] = {(float)far->sh.texture.width, (float)far->sh.texture.height};
  const float res[2] = {(float)ctx->rt_ws[dest].texture.width, (float)ctx->rt_ws[dest].texture.height};
  const float world_cell = NG_RC_WS_CELL;
  const float hash_size = (float)(ctx->ws_cpu.hash_size > 0 ? ctx->ws_cpu.hash_size : 64);
  BeginTextureMode(ctx->rt_ws[dest]);
  ClearBackground(BLACK);
  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  if (pass->sh.loc_resolution >= 0) {
    SetShaderValue(pass->sh.handle, pass->sh.loc_resolution, res, SHADER_UNIFORM_VEC2);
  }
  if (pass->loc_sky >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_sky, sky, SHADER_UNIFORM_VEC3);
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_far_origin");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, far->origin, SHADER_UNIFORM_VEC3);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_far_size");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, far->size, SHADER_UNIFORM_VEC3);
    }
  }
  {
    // agent: composer-2.5 | 2026-08-10 | LOD bands from camera eye | 4e0c43
    const float eye[3] = {ctx->camera.position.x, ctx->camera.position.y, ctx->camera.position.z};
    int loc = GetShaderLocation(pass->sh.handle, "ng_eye");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, eye, SHADER_UNIFORM_VEC3);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_world_cell");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &world_cell, SHADER_UNIFORM_FLOAT);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_hash_size");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &hash_size, SHADER_UNIFORM_FLOAT);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_sh_res");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, sres, SHADER_UNIFORM_VEC2);
    }
  }
  if (pass->loc_tex_depth >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_depth, ctx->rt_depth.texture);
  }
  if (pass->loc_tex_normal >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_normal, ctx->rt_normal.texture);
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_sh");
    if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc, far->sh.texture);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_meta");
    if (loc >= 0 && ctx->ws_cpu.sparse_tex_ready) {
      SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_probe_meta.texture);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_hash");
    if (loc >= 0 && ctx->ws_cpu.sparse_tex_ready) {
      SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_probe_hash.texture);
    }
  }
  mod_render_fs_draw(ctx->rt_depth.texture, ctx->rt_ws[dest].texture.width,
                     ctx->rt_ws[dest].texture.height);
  EndShaderMode();
  EndTextureMode();
  ctx->ws_ping = dest;
}

/** Screen blit resolved WS irr for irradiance debug. */
static void mod_render_rc_ws_view(ModRenderCtx *ctx) {
  NgRcPassShader *pass = &ctx->rc_ws_view;
  const NgRcWsClip *far = &ctx->ws_clip[NG_RC_WS_CLIP_FAR];
  const float sky[3] = {NG_RC_SKY.x, NG_RC_SKY.y, NG_RC_SKY.z};
  const float probe_res = (float)(ctx->ws_probe_n > 0 ? ctx->ws_probe_n : 12);
  const int ping = ctx->ws_ping & 1;
  int pw = 0;
  int ph = 0;
  mod_render_internal_size(ctx, &pw, &ph);
  const float res[2] = {(float)pw, (float)ph};

  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  if (pass->sh.loc_resolution >= 0) {
    SetShaderValue(pass->sh.handle, pass->sh.loc_resolution, res, SHADER_UNIFORM_VEC2);
  }
  if (pass->loc_sky >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_sky, sky, SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_ws_origin >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_ws_origin, far->origin, SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_ws_size >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_ws_size, far->size, SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_probe_res >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_probe_res, &probe_res, SHADER_UNIFORM_FLOAT);
  }
  if (pass->loc_tex_depth >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_depth, ctx->rt_depth.texture);
  }
  if (pass->loc_tex_irradiance_ws >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_irradiance_ws, ctx->rt_ws[ping].texture);
  }
  DrawRectangle(0, 0, pw, ph, WHITE);
  EndShaderMode();
}

/** WS debug: 0=probe id, 1=world frac, 2=grid, 3=culling, 4=probes-lod. */
static void mod_render_rc_ws_debug(ModRenderCtx *ctx, int mode) {
  // agent: composer-2.5 | 2026-08-11 | fix cull debug tex unit bind | a5a0fc
  // agent: composer-2.5 | 2026-08-11 | wipe look-at cube cull path | bf3c1d
  NgRcPassShader *pass = &ctx->rc_ws_debug;
  const float probe_res = (float)(ctx->ws_probe_n > 0 ? ctx->ws_probe_n : 12);
  const float grid_res = (float)NG_RC_WS_GRID_RES;
  const int ping = ctx->ws_ping & 1;
  int pw = 0;
  int ph = 0;
  mod_render_internal_size(ctx, &pw, &ph);
  const float res[2] = {(float)pw, (float)ph};

  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  if (pass->sh.loc_resolution >= 0) {
    SetShaderValue(pass->sh.handle, pass->sh.loc_resolution, res, SHADER_UNIFORM_VEC2);
  }
  if (pass->loc_ws_origin >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_ws_origin, ctx->ws_cpu.origin, SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_ws_size >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_ws_size, ctx->ws_cpu.size, SHADER_UNIFORM_VEC3);
  }
  {
    const float eye[3] = {ctx->camera.position.x, ctx->camera.position.y, ctx->camera.position.z};
    int loc = GetShaderLocation(pass->sh.handle, "ng_eye");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, eye, SHADER_UNIFORM_VEC3);
    }
  }
  if (pass->loc_probe_res >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_probe_res, &probe_res, SHADER_UNIFORM_FLOAT);
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_grid_res");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &grid_res, SHADER_UNIFORM_FLOAT);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_debug_mode");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &mode, SHADER_UNIFORM_INT);
    }
  }
  /* Flush slot table; bind EVERY sampler2D (GLES unbound units poison fetches). */
  // agent: composer-2.5 | 2026-08-11 | culling debug bind all samplers | 6e7512
  rlDrawRenderBatchActive();
  if (pass->loc_tex_depth >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_depth, ctx->rt_depth.texture);
  }
  if (mode == 3) {
    const float bvh_count = (float)(ctx->ws_cpu.bvh_count > 0 ? ctx->ws_cpu.bvh_count : 0);
    const float bvh_root = (float)ctx->ws_cpu.bvh_root;
    const float prim_count = (float)(ctx->ws_cpu.prim_count > 0 ? ctx->ws_cpu.prim_count : 0);
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_bvh_count");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &bvh_count, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_bvh_root");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &bvh_root, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_prim_count");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &prim_count, SHADER_UNIFORM_FLOAT);
      }
    }
    /* Dummy unused samplers → depth (same id reuses one unit). */
    // agent: composer-2.5 | 2026-08-11 | culling debug bind tex_prim | 43c334
    if (pass->loc_tex_irradiance_ws >= 0) {
      SetShaderValueTexture(pass->sh.handle, pass->loc_tex_irradiance_ws, ctx->rt_depth.texture);
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_hash");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_depth.texture);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_bvh");
      if (loc >= 0 && ctx->ws_cpu.bvh_tex_ready) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->ws_cpu.tex_bvh);
      } else if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_depth.texture);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_grid");
      if (loc >= 0 && ctx->ws_cpu.grid_tex_ready) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->ws_cpu.tex_grid);
      } else if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_depth.texture);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_prim");
      if (loc >= 0 && ctx->ws_cpu.prim_tex_ready) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->ws_cpu.tex_prim);
      } else if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_depth.texture);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_prim_vis");
      if (loc >= 0 && ctx->ws_cull_ready) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_prim_vis.texture);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_prim_id");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_prim_id.texture);
      }
    }
    /* Same carrier path as compose — FragCoord UV aligns with gbuf RTs. */
    mod_render_fs_draw(ctx->rt_depth.texture, pw, ph);
  } else {
    if (pass->loc_tex_irradiance_ws >= 0) {
      SetShaderValueTexture(pass->sh.handle, pass->loc_tex_irradiance_ws, ctx->rt_ws[ping].texture);
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_grid");
      if (loc >= 0 && ctx->ws_cpu.grid_tex_ready) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->ws_cpu.tex_grid);
      }
    }
    {
      const float world_cell = NG_RC_WS_CELL;
      int loc = GetShaderLocation(pass->sh.handle, "ng_world_cell");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &world_cell, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      const float hash_size = (float)(ctx->ws_cpu.hash_size > 0 ? ctx->ws_cpu.hash_size : 64);
      int loc = GetShaderLocation(pass->sh.handle, "ng_hash_size");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &hash_size, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_hash");
      if (loc >= 0 && ctx->ws_cpu.sparse_tex_ready) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_probe_hash.texture);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_probe_id");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_probe_id.texture);
      }
    }
    DrawRectangle(0, 0, pw, ph, WHITE);
  }
  EndShaderMode();
}

/** Build screen-space ID RTs from depth: prim_id and probe_id. */
static void mod_render_rc_ws_id_tick(ModRenderCtx *ctx) {
  // agent: composer-2.5 | 2026-08-12 | deferred prim probe id passes | 5f5d17
  if (!ctx->rc_ws_id.ready || !ctx->gbuf_ready || !ctx->ws_cpu.prim_tex_ready ||
      !ctx->ws_cpu.sparse_tex_ready) {
    return;
  }
  NgRcPassShader *pass = &ctx->rc_ws_id;
  const float world_cell = NG_RC_WS_CELL;
  const float hash_size = (float)(ctx->ws_cpu.hash_size > 0 ? ctx->ws_cpu.hash_size : 64);
  const float prim_count = (float)(ctx->ws_cpu.prim_count > 0 ? ctx->ws_cpu.prim_count : 0);
  int iw = 0;
  int ih = 0;
  mod_render_internal_size(ctx, &iw, &ih);
  const float res[2] = {(float)iw, (float)ih};
  for (int mode = 0; mode < 2; mode++) {
    RenderTexture2D *dest = (mode == 0) ? &ctx->rt_prim_id : &ctx->rt_probe_id;
    BeginTextureMode(*dest);
    ClearBackground(BLACK);
    BeginShaderMode(pass->sh.handle);
    ng_shader_set_common(&pass->sh, (float)GetTime());
    if (pass->sh.loc_resolution >= 0) {
      SetShaderValue(pass->sh.handle, pass->sh.loc_resolution, res, SHADER_UNIFORM_VEC2);
    }
    if (pass->loc_tex_depth >= 0) {
      SetShaderValueTexture(pass->sh.handle, pass->loc_tex_depth, ctx->rt_depth.texture);
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_prim");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->ws_cpu.tex_prim);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_hash");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_probe_hash.texture);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_world_cell");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &world_cell, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_hash_size");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &hash_size, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_prim_count");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &prim_count, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_id_mode");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &mode, SHADER_UNIFORM_INT);
      }
    }
    mod_render_fs_draw(ctx->rt_depth.texture, dest->texture.width, dest->texture.height);
    EndShaderMode();
    EndTextureMode();
  }
}

/** Shared look-at scroll with hysteresis; near nested in far (lockstep). */
static void mod_render_rc_ws_update_frustum_aabb(ModRenderCtx *ctx) {
  // agent: composer-2.5 | 2026-08-10 | lockstep clip scroll soft blend | 23e626
  const Camera3D *cam = &ctx->camera;
  const float cell = NG_RC_WS_CELL;
  const float near_extent = cell * (float)NG_RC_WS_VOX_RES;
  const float far_extent = near_extent * 2.0f;
  const Vector3 anchor = cam->target;

  /* One scroll for both LODs — independent snaps desync probe phase vs walls. */
  Vector3 desired = {anchor.x - far_extent * 0.5f, anchor.y - far_extent * 0.5f,
                     anchor.z - far_extent * 0.5f};
  const float snap = cell * 2.0f;
  desired.x = floorf(desired.x / snap) * snap;
  desired.y = floorf(desired.y / snap) * snap;
  desired.z = floorf(desired.z / snap) * snap;

  NgRcWsClip *far = &ctx->ws_clip[NG_RC_WS_CLIP_FAR];
  NgRcWsClip *near = &ctx->ws_clip[NG_RC_WS_CLIP_NEAR];
  Vector3 origin = {far->origin[0], far->origin[1], far->origin[2]};
  const float hold = far_extent * 0.25f;
  const int need_init = (far->size[0] < far_extent * 0.5f);
  if (need_init || fabsf(desired.x - origin.x) >= hold || fabsf(desired.y - origin.y) >= hold ||
      fabsf(desired.z - origin.z) >= hold) {
    origin = desired;
  }

  far->origin[0] = origin.x;
  far->origin[1] = origin.y;
  far->origin[2] = origin.z;
  far->size[0] = far_extent;
  far->size[1] = far_extent;
  far->size[2] = far_extent;

  /* Near locked to far center — never scroll independently (kills pan flicker). */
  const float nest = (far_extent - near_extent) * 0.5f;
  near->origin[0] = far->origin[0] + nest;
  near->origin[1] = far->origin[1] + nest;
  near->origin[2] = far->origin[2] + nest;
  near->size[0] = near_extent;
  near->size[1] = near_extent;
  near->size[2] = near_extent;

  ctx->ws_cpu.origin[0] = far->origin[0];
  ctx->ws_cpu.origin[1] = far->origin[1];
  ctx->ws_cpu.origin[2] = far->origin[2];
  ctx->ws_cpu.size[0] = far->size[0];
  ctx->ws_cpu.size[1] = far->size[1];
  ctx->ws_cpu.size[2] = far->size[2];
}

/** True if clip snapped (GI) or scene content changed. */
static bool mod_render_rc_ws_vox_dirty(ModRenderCtx *ctx) {
  // agent: composer-2.5 | 2026-08-11 | vox dirty scene hash only | ec6769
  const uint32_t h = ng_rc_ws_scene_hash();
#if NG_RC_WS_GI_OFFLINE
  if (h == ctx->ws_vox_scene_hash) {
    return false;
  }
  ctx->ws_vox_scene_hash = h;
  return true;
#else
  const float eps = 1e-3f;
  int moved = 0;
  for (int v = 0; v < NG_RC_WS_CLIP_COUNT; v++) {
    NgRcWsClip *clip = &ctx->ws_clip[v];
    if (fabsf(clip->origin[0] - clip->snap_origin[0]) > eps ||
        fabsf(clip->origin[1] - clip->snap_origin[1]) > eps ||
        fabsf(clip->origin[2] - clip->snap_origin[2]) > eps ||
        fabsf(clip->size[0] - clip->snap_size[0]) > eps ||
        fabsf(clip->size[1] - clip->snap_size[1]) > eps ||
        fabsf(clip->size[2] - clip->snap_size[2]) > eps) {
      moved = 1;
    }
  }
  if (!moved && h == ctx->ws_vox_scene_hash) {
    return false;
  }
  for (int v = 0; v < NG_RC_WS_CLIP_COUNT; v++) {
    NgRcWsClip *clip = &ctx->ws_clip[v];
    clip->snap_origin[0] = clip->origin[0];
    clip->snap_origin[1] = clip->origin[1];
    clip->snap_origin[2] = clip->origin[2];
    clip->snap_size[0] = clip->size[0];
    clip->snap_size[1] = clip->size[1];
    clip->snap_size[2] = clip->size[2];
  }
  ctx->ws_vox_scene_hash = h;
  return true;
#endif
}

/** Inward frustum planes from camera basis (BeginMode3D look/fov). */
static void mod_render_frustum_planes(const Camera3D *cam, float aspect, Vector4 out[6]) {
  // agent: composer-2.5 | 2026-08-12 | camera-basis inward frustum planes | 8e0518
  Vector3 eye = cam->position;
  Vector3 f = Vector3Normalize(Vector3Subtract(cam->target, cam->position));
  Vector3 r = Vector3Normalize(Vector3CrossProduct(f, cam->up));
  Vector3 u = Vector3CrossProduct(r, f);
  const float znear = (float)rlGetCullDistanceNear();
  const float zfar = (float)rlGetCullDistanceFar();
  const float hv = tanf(cam->fovy * DEG2RAD * 0.5f);
  const float hh = hv * aspect;

  Vector3 nc = Vector3Add(eye, Vector3Scale(f, znear));
  Vector3 fc = Vector3Add(eye, Vector3Scale(f, zfar));

  /* near / far — normals point inward. */
  out[4].x = f.x;
  out[4].y = f.y;
  out[4].z = f.z;
  out[4].w = -Vector3DotProduct(f, nc);
  out[5].x = -f.x;
  out[5].y = -f.y;
  out[5].z = -f.z;
  out[5].w = Vector3DotProduct(f, fc);

  /* Side planes through eye; n = u × edge (inward). */
  {
    Vector3 left_dir = Vector3Normalize(Vector3Subtract(f, Vector3Scale(r, hh)));
    Vector3 n = Vector3Normalize(Vector3CrossProduct(u, left_dir));
    out[0].x = n.x;
    out[0].y = n.y;
    out[0].z = n.z;
    out[0].w = -Vector3DotProduct(n, eye);
  }
  {
    Vector3 right_dir = Vector3Normalize(Vector3Add(f, Vector3Scale(r, hh)));
    Vector3 n = Vector3Normalize(Vector3CrossProduct(right_dir, u));
    out[1].x = n.x;
    out[1].y = n.y;
    out[1].z = n.z;
    out[1].w = -Vector3DotProduct(n, eye);
  }
  {
    Vector3 bottom_dir = Vector3Normalize(Vector3Subtract(f, Vector3Scale(u, hv)));
    Vector3 n = Vector3Normalize(Vector3CrossProduct(bottom_dir, r));
    out[2].x = n.x;
    out[2].y = n.y;
    out[2].z = n.z;
    out[2].w = -Vector3DotProduct(n, eye);
  }
  {
    Vector3 top_dir = Vector3Normalize(Vector3Add(f, Vector3Scale(u, hv)));
    Vector3 n = Vector3Normalize(Vector3CrossProduct(r, top_dir));
    out[3].x = n.x;
    out[3].y = n.y;
    out[3].z = n.z;
    out[3].w = -Vector3DotProduct(n, eye);
  }

  /* Orient each plane so look-at is inside (sides may need flip; near/far usually ok). */
  // agent: composer-2.5 | 2026-08-12 | frustum per-plane target orient | 8a11d4
  {
    Vector3 t = cam->target;
    for (int i = 0; i < 6; i++) {
      if (out[i].x * t.x + out[i].y * t.y + out[i].z * t.z + out[i].w < 0.0f) {
        out[i].x = -out[i].x;
        out[i].y = -out[i].y;
        out[i].z = -out[i].z;
        out[i].w = -out[i].w;
      }
    }
  }
}

/** Blit curr prim vis → prev (probes consume prev next frame). */
static void mod_render_rc_ws_vis_swap(ModRenderCtx *ctx) {
  // agent: composer-2.5 | 2026-08-12 | VS cull double-buffer vis | aac081
  if (!ctx->ws_cull_ready) {
    return;
  }
  BeginTextureMode(ctx->rt_prim_vis_prev);
  ClearBackground(BLACK);
  const Rectangle src = {0, 0, (float)ctx->rt_prim_vis.texture.width,
                         (float)-ctx->rt_prim_vis.texture.height};
  const Rectangle dst = {0, 0, (float)ctx->rt_prim_vis_prev.texture.width,
                         (float)ctx->rt_prim_vis_prev.texture.height};
  DrawTexturePro(ctx->rt_prim_vis.texture, src, dst, (Vector2){0, 0}, 0.0f, WHITE);
  EndTextureMode();
}

/** CPU BVH frustum cull → inst bitset + upload tex_prim_vis. */
static void mod_render_rc_ws_cull(ModRenderCtx *ctx) {
  // agent: composer-2.5 | 2026-08-13 | CPU cull tick upload vis | 4d378d
  ctx->ws_frustum_valid = false;
  if (!ctx->ws_cull_ready || !ctx->ws_cpu.prim_tex_ready || !ctx->ws_cpu.bvh_tex_ready) {
    ctx->ws_cpu.cull_valid = false;
    return;
  }
  if (ctx->ws_cpu.prim_count <= 0) {
    ctx->ws_cpu.cull_valid = false;
    ctx->ws_cpu.cull_vis_n = 0;
    return;
  }

  int iw = 0;
  int ih = 0;
  mod_render_internal_size(ctx, &iw, &ih);
  if (iw < 1) {
    iw = 1;
  }
  if (ih < 1) {
    ih = 1;
  }
  const float aspect = (float)iw / (float)ih;
  Vector4 planes[6];
  mod_render_frustum_planes(&ctx->camera, aspect, planes);
  for (int i = 0; i < 6; i++) {
    ctx->ws_frustum[i] = planes[i];
  }
  ctx->ws_frustum_valid = true;

  (void)ng_rc_ws_cull_traverse(&ctx->ws_cpu, planes, NULL, 0);
  ng_rc_ws_upload_prim_vis(&ctx->ws_cpu, ctx->rt_prim_vis.texture);
}

/** GPU keep: work + prim + prim_vis → rt_probe_keep (coarse AABB / fine shell). */
static void mod_render_rc_ws_probe_keep(ModRenderCtx *ctx, int nwork, int probe_any,
                                       Texture2D prim_vis) {
  // agent: composer-2.5 | 2026-08-12 | coarse AABB fine shell keep | 9119d3
  if (nwork <= 0 || !ctx->ws_probe_ready || !ctx->rc_ws_probe.ready) {
    return;
  }
  if (nwork > NG_RC_WS_PROBE_WORK_MAX) {
    nwork = NG_RC_WS_PROBE_WORK_MAX;
  }
  NgRcPassShader *pass = &ctx->rc_ws_probe;
  const float work_count = (float)nwork;
  const float prim_count = (float)ctx->ws_cpu.prim_count;
  const float world_cell = NG_RC_WS_CELL;
  const float shell_lod_max = (float)(NG_RC_WS_LOD_MAX - 2); /* ~25.6m — shell reliable */
  rlDrawRenderBatchActive();
  rlDisableColorBlend();
  BeginTextureMode(ctx->rt_probe_keep);
  ClearBackground(BLACK);
  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_work_count");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &work_count, SHADER_UNIFORM_FLOAT);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_prim_count");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &prim_count, SHADER_UNIFORM_FLOAT);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_world_cell");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &world_cell, SHADER_UNIFORM_FLOAT);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_shell_lod_max");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &shell_lod_max, SHADER_UNIFORM_FLOAT);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_probe_any");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &probe_any, SHADER_UNIFORM_INT);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_work");
    if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_probe_work.texture);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_prim");
    if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc, ctx->ws_cpu.tex_prim);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_prim_vis");
    if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc, prim_vis);
    }
  }
  DrawRectangle(0, 0, ctx->rt_probe_keep.texture.width, ctx->rt_probe_keep.texture.height, WHITE);
  EndShaderMode();
  EndTextureMode();
  rlEnableColorBlend();
}

/** Apply / meta / hash. Pass 3 ping-pongs slots. */
static void mod_render_rc_ws_probe_apply_passes(ModRenderCtx *ctx, int nwork, int pass_id) {
  // agent: composer-2.5 | 2026-08-12 | probe tick single-root wire | 70b763
  // agent: composer-2.5 | 2026-08-12 | lazy stochastic split steal | 1c5864
  NgRcPassShader *pass = &ctx->rc_ws_probe_apply;
  if (!pass->ready) {
    return;
  }
  const float work_count = (float)nwork;
  const float slot_cap = (float)ctx->ws_cpu.slot_cap;
  const float budget = (float)(ctx->ws_cpu.slot_cap / 2);
  const float hash_size = (float)ctx->ws_cpu.hash_size;
  const float world_cell = NG_RC_WS_CELL;
  const float split_k = 16.0f;
  const float frame_f = (float)ctx->probe_frame;
  rlDrawRenderBatchActive();
  rlDisableColorBlend();
  RenderTexture2D *dst = &ctx->rt_probe_hash;
  if (pass_id == 1) {
    dst = &ctx->rt_probe_meta;
  } else if (pass_id == 3) {
    dst = &ctx->rt_probe_slots_b;
  }
  BeginTextureMode(*dst);
  ClearBackground(BLANK);
  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_work_count");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &work_count, SHADER_UNIFORM_FLOAT);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_slot_cap");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &slot_cap, SHADER_UNIFORM_FLOAT);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_budget");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &budget, SHADER_UNIFORM_FLOAT);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_hash_size");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &hash_size, SHADER_UNIFORM_FLOAT);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_world_cell");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &world_cell, SHADER_UNIFORM_FLOAT);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_split_k");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &split_k, SHADER_UNIFORM_FLOAT);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_frame");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &frame_f, SHADER_UNIFORM_FLOAT);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_apply_pass");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &pass_id, SHADER_UNIFORM_INT);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_work");
    if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_probe_work.texture);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_keep");
    if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_probe_keep.texture);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_slots");
    if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_probe_slots.texture);
    }
  }
  DrawRectangle(0, 0, dst->texture.width, dst->texture.height, WHITE);
  EndShaderMode();
  EndTextureMode();
  rlEnableColorBlend();
  if (pass_id == 3) {
    RenderTexture2D tmp = ctx->rt_probe_slots;
    ctx->rt_probe_slots = ctx->rt_probe_slots_b;
    ctx->rt_probe_slots_b = tmp;
  }
}

/** Swap probe slot ping-pong RTs. */
static void mod_render_rc_ws_probe_slots_swap(ModRenderCtx *ctx) {
  RenderTexture2D tmp = ctx->rt_probe_slots;
  ctx->rt_probe_slots = ctx->rt_probe_slots_b;
  ctx->rt_probe_slots_b = tmp;
}

/**
 * GPU incremental probes: union → release → cover → split → unmet+relax →
 * over-budget steal → stats → meta/hash. Persist keys; relax only if stuck.
 */
static void mod_render_rc_ws_probe_tick(ModRenderCtx *ctx) {
  // agent: composer-2.5 | 2026-08-12 | GPU probe tick CPU order | c1f502
  // agent: composer-2.5 | 2026-08-12 | probe incremental residency tick | 5af26a
  // agent: grok-4.6 | 2026-08-12 | split then steal skip compact | 680d06
  // agent: grok-4.6 | 2026-08-12 | persist probe tick GPU | cabbaa
  NgRcWsCtx *ws = &ctx->ws_cpu;
  if (!ctx->ws_probe_ready || !ctx->ws_cull_ready || !ctx->rc_ws_probe_cover.ready ||
      !ctx->rc_ws_probe_apply.ready || !ctx->rc_ws_probe.ready || !ctx->rc_ws_probe_union.ready ||
      !ctx->rc_ws_probe_release.ready || !ctx->rc_ws_probe_merge.ready ||
      !ctx->rc_ws_probe_steal.ready || !ctx->rc_ws_probe_stats.ready ||
      !ctx->rc_ws_probe_unmet.ready || !ctx->rc_ws_probe_relax.ready || ws->prim_count <= 0) {
    return;
  }
  const float eye[3] = {ctx->camera.position.x, ctx->camera.position.y, ctx->camera.position.z};
  Vector3 fwd = Vector3Normalize(Vector3Subtract(ctx->camera.target, ctx->camera.position));
  const float forward[3] = {fwd.x, fwd.y, fwd.z};
  const uint32_t fp = ng_rc_ws_probe_view_fp(ws, eye, forward);
  const bool view_moved = !ctx->probe_gpu_valid || fp != ctx->probe_fp;
  ctx->probe_fp = fp;

  const int budget = ws->slot_cap / 2;
  const int clod = NG_RC_WS_LOD_MAX - 2;
  const float prim_count = (float)ws->prim_count;
  const float world_cell = NG_RC_WS_CELL;
  const float slot_cap_f = (float)ws->slot_cap;
  const float lod_soft = (float)NG_RC_WS_LOD_SOFT_MAX;
  const float cover_lod = (float)clod;
  const float budget_f = (float)budget;
  const float shell_lod_max = (float)clod;
  Texture2D prim_vis = ctx->rt_prim_vis.texture;

  /* Cold reset: GPU clear only (scene dirty / first valid). */
  if (!ctx->probe_gpu_valid) {
    rlDrawRenderBatchActive();
    rlDisableColorBlend();
    BeginTextureMode(ctx->rt_probe_slots);
    ClearBackground(BLANK);
    EndTextureMode();
    BeginTextureMode(ctx->rt_probe_slots_b);
    ClearBackground(BLANK);
    EndTextureMode();
    BeginTextureMode(ctx->rt_probe_hash);
    ClearBackground(BLANK);
    EndTextureMode();
    BeginTextureMode(ctx->rt_probe_meta);
    ClearBackground(BLANK);
    EndTextureMode();
    rlEnableColorBlend();
  }

  /* 1) Vis-union AABB → rt_probe_union */
  {
    NgRcPassShader *pass = &ctx->rc_ws_probe_union;
    rlDrawRenderBatchActive();
    rlDisableColorBlend();
    BeginTextureMode(ctx->rt_probe_union);
    ClearBackground(BLANK);
    BeginShaderMode(pass->sh.handle);
    ng_shader_set_common(&pass->sh, (float)GetTime());
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_prim_count");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &prim_count, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_prim");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ws->tex_prim);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_prim_vis");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, prim_vis);
      }
    }
    DrawRectangle(0, 0, ctx->rt_probe_union.texture.width, ctx->rt_probe_union.texture.height,
                  WHITE);
    EndShaderMode();
    EndTextureMode();
    rlEnableColorBlend();
  }

  /* 2) Release outside AABB / empty shell */
  {
    NgRcPassShader *pass = &ctx->rc_ws_probe_release;
    rlDrawRenderBatchActive();
    rlDisableColorBlend();
    BeginTextureMode(ctx->rt_probe_slots_b);
    ClearBackground(BLANK);
    BeginShaderMode(pass->sh.handle);
    ng_shader_set_common(&pass->sh, (float)GetTime());
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_prim_count");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &prim_count, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_world_cell");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &world_cell, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_slot_cap");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &slot_cap_f, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_shell_lod_max");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &shell_lod_max, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_slots");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_probe_slots.texture);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_prim");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ws->tex_prim);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_prim_vis");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, prim_vis);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_union");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_probe_union.texture);
      }
    }
    DrawRectangle(0, 0, ctx->rt_probe_slots_b.texture.width, ctx->rt_probe_slots_b.texture.height,
                  WHITE);
    EndShaderMode();
    EndTextureMode();
    rlEnableColorBlend();
    mod_render_rc_ws_probe_slots_swap(ctx);
  }

  /* 3) Lazy cover: keep used; insert ≤K missing surface cells (no compact). */
  // agent: grok-4.6 | 2026-08-12 | split then steal skip compact | 680d06
  // agent: grok-4.6 | 2026-08-12 | persist probe tick GPU | cabbaa
  {
    NgRcPassShader *pass = &ctx->rc_ws_probe_cover;
    rlDrawRenderBatchActive();
    rlDisableColorBlend();
    BeginTextureMode(ctx->rt_probe_slots_b);
    ClearBackground(BLANK);
    BeginShaderMode(pass->sh.handle);
    ng_shader_set_common(&pass->sh, (float)GetTime());
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_prim_count");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &prim_count, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_world_cell");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &world_cell, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_slot_cap");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &slot_cap_f, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_lod_soft_max");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &lod_soft, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_cover_lod");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &cover_lod, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_budget");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &budget_f, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      const float cover_k = 64.0f;
      int loc = GetShaderLocation(pass->sh.handle, "ng_cover_k");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &cover_k, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_slots");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_probe_slots.texture);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_union");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_probe_union.texture);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_prim");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ws->tex_prim);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_prim_vis");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, prim_vis);
      }
    }
    DrawRectangle(0, 0, ctx->rt_probe_slots_b.texture.width, ctx->rt_probe_slots_b.texture.height,
                  WHITE);
    EndShaderMode();
    EndTextureMode();
    rlEnableColorBlend();
    mod_render_rc_ws_probe_slots_swap(ctx);
  }

  /* 4) Unmet flag → relax if stuck at budget. */
  int gens = 0;
  {
    NgRcPassShader *upass = &ctx->rc_ws_probe_unmet;
    rlDrawRenderBatchActive();
    rlDisableColorBlend();
    BeginTextureMode(ctx->rt_probe_unmet);
    ClearBackground(BLANK);
    BeginShaderMode(upass->sh.handle);
    ng_shader_set_common(&upass->sh, (float)GetTime());
    {
      int loc = GetShaderLocation(upass->sh.handle, "ng_prim_count");
      if (loc >= 0) {
        SetShaderValue(upass->sh.handle, loc, &prim_count, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(upass->sh.handle, "ng_world_cell");
      if (loc >= 0) {
        SetShaderValue(upass->sh.handle, loc, &world_cell, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(upass->sh.handle, "ng_slot_cap");
      if (loc >= 0) {
        SetShaderValue(upass->sh.handle, loc, &slot_cap_f, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(upass->sh.handle, "ng_cover_lod");
      if (loc >= 0) {
        SetShaderValue(upass->sh.handle, loc, &cover_lod, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(upass->sh.handle, "ng_lod_soft_max");
      if (loc >= 0) {
        SetShaderValue(upass->sh.handle, loc, &lod_soft, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(upass->sh.handle, "tex_slots");
      if (loc >= 0) {
        SetShaderValueTexture(upass->sh.handle, loc, ctx->rt_probe_slots.texture);
      }
    }
    {
      int loc = GetShaderLocation(upass->sh.handle, "tex_union");
      if (loc >= 0) {
        SetShaderValueTexture(upass->sh.handle, loc, ctx->rt_probe_union.texture);
      }
    }
    {
      int loc = GetShaderLocation(upass->sh.handle, "tex_prim");
      if (loc >= 0) {
        SetShaderValueTexture(upass->sh.handle, loc, ws->tex_prim);
      }
    }
    {
      int loc = GetShaderLocation(upass->sh.handle, "tex_prim_vis");
      if (loc >= 0) {
        SetShaderValueTexture(upass->sh.handle, loc, prim_vis);
      }
    }
    DrawRectangle(0, 0, 1, 1, WHITE);
    EndShaderMode();
    EndTextureMode();
    rlEnableColorBlend();
  }
  {
    const float relax_k = 16.0f;
    NgRcPassShader *pass = &ctx->rc_ws_probe_relax;
    rlDrawRenderBatchActive();
    rlDisableColorBlend();
    BeginTextureMode(ctx->rt_probe_slots_b);
    ClearBackground(BLANK);
    BeginShaderMode(pass->sh.handle);
    ng_shader_set_common(&pass->sh, (float)GetTime());
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_slot_cap");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &slot_cap_f, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_budget");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &budget_f, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_relax_k");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &relax_k, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_slots");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_probe_slots.texture);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_unmet");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_probe_unmet.texture);
      }
    }
    DrawRectangle(0, 0, ctx->rt_probe_slots_b.texture.width, ctx->rt_probe_slots_b.texture.height,
                  WHITE);
    EndShaderMode();
    EndTextureMode();
    rlEnableColorBlend();
    mod_render_rc_ws_probe_slots_swap(ctx);
  }

  /* 5) Cover again after relax freed slots. */
  // agent: composer-2.5 | 2026-08-12 | GPU cover after relax pass | ad8ebb
  {
    NgRcPassShader *pass = &ctx->rc_ws_probe_cover;
    rlDrawRenderBatchActive();
    rlDisableColorBlend();
    BeginTextureMode(ctx->rt_probe_slots_b);
    ClearBackground(BLANK);
    BeginShaderMode(pass->sh.handle);
    ng_shader_set_common(&pass->sh, (float)GetTime());
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_prim_count");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &prim_count, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_world_cell");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &world_cell, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_slot_cap");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &slot_cap_f, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_lod_soft_max");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &lod_soft, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_cover_lod");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &cover_lod, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_budget");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &budget_f, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      const float cover_k = 64.0f;
      int loc = GetShaderLocation(pass->sh.handle, "ng_cover_k");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &cover_k, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_slots");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_probe_slots.texture);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_union");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_probe_union.texture);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_prim");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ws->tex_prim);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_prim_vis");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, prim_vis);
      }
    }
    DrawRectangle(0, 0, ctx->rt_probe_slots_b.texture.width, ctx->rt_probe_slots_b.texture.height,
                  WHITE);
    EndShaderMode();
    EndTextureMode();
    rlEnableColorBlend();
    mod_render_rc_ws_probe_slots_swap(ctx);
  }

  /* 6) Split gate: GPU unmet + lod_max → allow_split in tex_unmet.b. */
  // agent: composer-2.5 | 2026-08-13 | GPU split gate no readback | f07898
  {
    NgRcPassShader *upass = &ctx->rc_ws_probe_unmet;
    rlDrawRenderBatchActive();
    rlDisableColorBlend();
    BeginTextureMode(ctx->rt_probe_unmet);
    ClearBackground(BLANK);
    BeginShaderMode(upass->sh.handle);
    ng_shader_set_common(&upass->sh, (float)GetTime());
    {
      int loc = GetShaderLocation(upass->sh.handle, "ng_prim_count");
      if (loc >= 0) {
        SetShaderValue(upass->sh.handle, loc, &prim_count, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(upass->sh.handle, "ng_world_cell");
      if (loc >= 0) {
        SetShaderValue(upass->sh.handle, loc, &world_cell, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(upass->sh.handle, "ng_slot_cap");
      if (loc >= 0) {
        SetShaderValue(upass->sh.handle, loc, &slot_cap_f, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(upass->sh.handle, "ng_cover_lod");
      if (loc >= 0) {
        SetShaderValue(upass->sh.handle, loc, &cover_lod, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(upass->sh.handle, "ng_lod_soft_max");
      if (loc >= 0) {
        SetShaderValue(upass->sh.handle, loc, &lod_soft, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(upass->sh.handle, "tex_slots");
      if (loc >= 0) {
        SetShaderValueTexture(upass->sh.handle, loc, ctx->rt_probe_slots.texture);
      }
    }
    {
      int loc = GetShaderLocation(upass->sh.handle, "tex_union");
      if (loc >= 0) {
        SetShaderValueTexture(upass->sh.handle, loc, ctx->rt_probe_union.texture);
      }
    }
    {
      int loc = GetShaderLocation(upass->sh.handle, "tex_prim");
      if (loc >= 0) {
        SetShaderValueTexture(upass->sh.handle, loc, ws->tex_prim);
      }
    }
    {
      int loc = GetShaderLocation(upass->sh.handle, "tex_prim_vis");
      if (loc >= 0) {
        SetShaderValueTexture(upass->sh.handle, loc, prim_vis);
      }
    }
    DrawRectangle(0, 0, 1, 1, WHITE);
    EndShaderMode();
    EndTextureMode();
    rlEnableColorBlend();
  }
  if (ctx->rc_ws_probe_gen.ready) {
    int nwork = ws->slot_cap * 8;
    if (nwork > NG_RC_WS_PROBE_WORK_MAX) {
      nwork = NG_RC_WS_PROBE_WORK_MAX;
    }
    const float nwork_f = (float)nwork;
    NgRcPassShader *gpass = &ctx->rc_ws_probe_gen;
    rlDrawRenderBatchActive();
    rlDisableColorBlend();
    BeginTextureMode(ctx->rt_probe_work);
    ClearBackground(BLANK);
    BeginShaderMode(gpass->sh.handle);
    ng_shader_set_common(&gpass->sh, (float)GetTime());
    {
      int loc = GetShaderLocation(gpass->sh.handle, "ng_slot_cap");
      if (loc >= 0) {
        SetShaderValue(gpass->sh.handle, loc, &slot_cap_f, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(gpass->sh.handle, "ng_work_count");
      if (loc >= 0) {
        SetShaderValue(gpass->sh.handle, loc, &nwork_f, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(gpass->sh.handle, "tex_slots");
      if (loc >= 0) {
        SetShaderValueTexture(gpass->sh.handle, loc, ctx->rt_probe_slots.texture);
      }
    }
    {
      int loc = GetShaderLocation(gpass->sh.handle, "tex_unmet");
      if (loc >= 0) {
        SetShaderValueTexture(gpass->sh.handle, loc, ctx->rt_probe_unmet.texture);
      }
    }
    DrawRectangle(0, 0, ctx->rt_probe_work.texture.width, ctx->rt_probe_work.texture.height, WHITE);
    EndShaderMode();
    EndTextureMode();
    rlEnableColorBlend();
    mod_render_rc_ws_probe_keep(ctx, nwork, 1, prim_vis);
    mod_render_rc_ws_probe_apply_passes(ctx, nwork, 3);
    gens = 1;
  }

  /* 7) Over-budget steal finest. */
  {
    const float steal_k = 16.0f;
    NgRcPassShader *pass = &ctx->rc_ws_probe_steal;
    rlDrawRenderBatchActive();
    rlDisableColorBlend();
    BeginTextureMode(ctx->rt_probe_slots_b);
    ClearBackground(BLANK);
    BeginShaderMode(pass->sh.handle);
    ng_shader_set_common(&pass->sh, (float)GetTime());
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_slot_cap");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &slot_cap_f, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_budget");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &budget_f, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_steal_k");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &steal_k, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_slots");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_probe_slots.texture);
      }
    }
    DrawRectangle(0, 0, ctx->rt_probe_slots_b.texture.width, ctx->rt_probe_slots_b.texture.height,
                  WHITE);
    EndShaderMode();
    EndTextureMode();
    rlEnableColorBlend();
    mod_render_rc_ws_probe_slots_swap(ctx);
  }

  /* 7) Occupancy stats 1×1 */
  if (ctx->rc_ws_probe_stats.ready) {
    NgRcPassShader *pass = &ctx->rc_ws_probe_stats;
    rlDrawRenderBatchActive();
    rlDisableColorBlend();
    BeginTextureMode(ctx->rt_probe_stats);
    ClearBackground(BLANK);
    BeginShaderMode(pass->sh.handle);
    ng_shader_set_common(&pass->sh, (float)GetTime());
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_slot_cap");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &slot_cap_f, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "ng_budget");
      if (loc >= 0) {
        SetShaderValue(pass->sh.handle, loc, &budget_f, SHADER_UNIFORM_FLOAT);
      }
    }
    {
      int loc = GetShaderLocation(pass->sh.handle, "tex_slots");
      if (loc >= 0) {
        SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_probe_slots.texture);
      }
    }
    DrawRectangle(0, 0, 1, 1, WHITE);
    EndShaderMode();
    EndTextureMode();
    rlEnableColorBlend();
  }

  /* 8) Meta + hash from resident slots (GPU only). */
  mod_render_rc_ws_probe_apply_passes(ctx, 0, 1);
  mod_render_rc_ws_probe_apply_passes(ctx, 0, 2);
  ctx->probe_gpu_valid = true;
  ctx->probe_frame++;

  if (ctx->debug_logging_probes && view_moved && ctx->rt_probe_stats.id != 0) {
    Image img = LoadImageFromTexture(ctx->rt_probe_stats.texture);
    int used_n = 0;
    int freeable_n = ws->slot_cap;
    if (img.data && img.width >= 1 && img.height >= 1) {
      const float *px = (const float *)img.data;
      used_n = (int)(px[0] + 0.5f);
      freeable_n = (int)(px[1] + 0.5f);
    }
    UnloadImage(img);
    int headroom_n = budget - used_n;
    if (headroom_n < 0) {
      headroom_n = 0;
    }
    TraceLog(LOG_INFO,
             "rc-ws probe used=%d free=%d headroom=%d budget=%d/%d gens=%d cover_lod=%d prims=%d",
             used_n, freeable_n, headroom_n, budget, ws->slot_cap, gens, clod, ws->prim_count);
  }
}

/** Rebuild prims/BVH when dirty; GPU cull + probe cover (gbuf later). */
static void mod_render_rc_gpu_tick(ModRenderCtx *ctx) {
  // agent: composer-2.5 | 2026-08-11 | BVH frustum cull foundation wire | 868ee4
  // agent: composer-2.5 | 2026-08-12 | GPU probe tick wire | 7f001b
  const int force = mod_render_rc_ws_vox_dirty(ctx) ? 1 : 0;
  if (force || ctx->ws_cpu.prim_count <= 0) {
    ng_rc_ws_rebuild_prims(&ctx->ws_cpu);
    ctx->probe_fp = 0;
    ctx->probe_gpu_valid = false;
#if !NG_RC_WS_GI_OFFLINE
    ng_rc_ws_rebuild_grid(&ctx->ws_cpu);
#endif
  }
#if NG_RC_WS_GI_OFFLINE
  // agent: composer-2.5 | 2026-08-11 | wipe look-at cube cull path | bf3c1d
  // agent: composer-2.5 | 2026-08-11 | skip clip grid when GI off | 8bfa78
  // agent: composer-2.5 | 2026-08-12 | VS cull double-buffer vis | aac081
  (void)ctx->ws_prio_gpu_ready;
  mod_render_rc_ws_cull(ctx);
  return;
#else
  NgRcWsClip *far = &ctx->ws_clip[NG_RC_WS_CLIP_FAR];
  {
    const float eye[3] = {ctx->camera.position.x, ctx->camera.position.y, ctx->camera.position.z};
    Vector3 fwd = Vector3Normalize(Vector3Subtract(ctx->camera.target, ctx->camera.position));
    const float forward[3] = {fwd.x, fwd.y, fwd.z};
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    if (sw < 1) {
      sw = 1;
    }
    if (sh < 1) {
      sh = 1;
    }
    const float aspect = (float)sw / (float)sh;
    const float thv = tanf(ctx->camera.fovy * DEG2RAD * 0.5f);
    ng_rc_ws_set_view(&ctx->ws_cpu, forward, thv, aspect);
    ng_rc_ws_sparse_tick(&ctx->ws_cpu, far->origin, far->size, eye);
    (void)ctx->ws_prio_gpu_ready;
  }
  if (force) {
    ng_rc_ws_sparse_mark_dirty(&ctx->ws_cpu);
  }

  const int dirty_n = ng_rc_ws_dirty_count(&ctx->ws_cpu);
  if (force || dirty_n > 0) {
    mod_render_rc_ws_fill_volume(ctx, far, force ? 0 : 1);
    ng_rc_ws_sparse_clear_dirty(&ctx->ws_cpu);
  }
  mod_render_rc_ws_resolve(ctx, ctx->ws_ping & 1);
  ctx->ws_frame++;

  if (ctx->ss_weight <= 0.0f) {
    return;
  }

  int cascades = 3;
  int shift = 0;
  int dir_side[NG_RC_CASCADES_MAX];
  int steps = 6;
  mod_render_rc_cost(ctx->rc_quality, &cascades, &shift, dir_side, &steps);
  (void)shift;
  (void)dir_side;

  static const float k_t0[NG_RC_CASCADES_MAX] = {0.0f, 16.0f, 64.0f};
  static const float k_t1[NG_RC_CASCADES_MAX] = {16.0f, 64.0f, 256.0f};
  for (int c = cascades - 1; c >= 0; c--) {
    if (ctx->rc_dir_side[c] <= 0) {
      continue;
    }
    mod_render_rc_fill_cascade(ctx, c, k_t0[c], k_t1[c], steps);
  }
  for (int c = cascades - 2; c >= 0; c--) {
    if (ctx->rc_dir_side[c] <= 0 || ctx->rc_dir_side[c + 1] <= 0) {
      continue;
    }
    mod_render_rc_merge(ctx, c, c + 1);
  }
  mod_render_rc_resolve(ctx);
#endif
}

/** Load RGBA32F color + depth renderbuffer FBO (world XYZ packing). */
static RenderTexture2D mod_render_load_rt_rgba32f(int width, int height) {
  // agent: composer-2.5 | 2026-08-11 | wire depth extent uniforms | 8ff0b2
  RenderTexture2D target = {0};
  if (width < 1 || height < 1) {
    return target;
  }
  target.id = rlLoadFramebuffer();
  if (target.id == 0) {
    return target;
  }
  rlEnableFramebuffer(target.id);
  target.texture.id =
      rlLoadTexture(NULL, width, height, PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1);
  target.texture.width = width;
  target.texture.height = height;
  target.texture.format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;
  target.texture.mipmaps = 1;
  target.depth.id = rlLoadTextureDepth(width, height, true);
  target.depth.width = width;
  target.depth.height = height;
  target.depth.format = 19;
  target.depth.mipmaps = 1;
  rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0,
                      RL_ATTACHMENT_TEXTURE2D, 0);
  rlFramebufferAttach(target.id, target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER,
                      0);
  if (!rlFramebufferComplete(target.id)) {
    TraceLog(LOG_ERROR, "rc-ws depth FBO incomplete");
  }
  rlDisableFramebuffer();
  SetTextureFilter(target.texture, TEXTURE_FILTER_POINT);
  SetTextureWrap(target.texture, TEXTURE_WRAP_CLAMP);
  return target;
}

static bool mod_render_ensure_gbuf(ModRenderCtx *ctx) {
  int w = 0;
  int h = 0;
  mod_render_internal_size(ctx, &w, &h);
  if (w <= 0 || h <= 0) {
    return false;
  }
  if (!ctx->gbuf_shader_ready) {
    // agent: composer-2.5 | 2026-08-12 | VS cull double-buffer vis | aac081
    ctx->gbuf_shader = ng_shader_load(NG_RES_ROOT "shaders/rc_gbuf.vs", NG_RES_ROOT "shaders/rc_gbuf.fs");
    if (ctx->gbuf_shader.handle.id == 0) {
      return false;
    }
    ctx->gbuf_shader_ready = true;
  }
  if (ctx->gbuf_ready && ctx->gbuf_w == w && ctx->gbuf_h == h) {
    return true;
  }
  if (ctx->gbuf_ready) {
    UnloadRenderTexture(ctx->rt_albedo);
    UnloadRenderTexture(ctx->rt_normal);
    UnloadRenderTexture(ctx->rt_glow);
    UnloadRenderTexture(ctx->rt_depth);
    ctx->gbuf_ready = false;
  }
  ctx->rt_albedo = LoadRenderTexture(w, h);
  ctx->rt_normal = LoadRenderTexture(w, h);
  ctx->rt_glow = LoadRenderTexture(w, h);
  ctx->rt_prim_id = mod_render_load_rt_rgba32f(w, h);
  ctx->rt_probe_id = mod_render_load_rt_rgba32f(w, h);
  /* Float depth: world XYZ without RGBA8 UVW clamp (culling debug association). */
  ctx->rt_depth = mod_render_load_rt_rgba32f(w, h);
  if (ctx->rt_depth.id == 0 || ctx->rt_depth.texture.id == 0 || ctx->rt_prim_id.id == 0 ||
      ctx->rt_prim_id.texture.id == 0 || ctx->rt_probe_id.id == 0 ||
      ctx->rt_probe_id.texture.id == 0) {
    return false;
  }
  ctx->gbuf_w = w;
  ctx->gbuf_h = h;
  ctx->gbuf_ready = true;
  return true;
}

static void mod_render_draw_batch(const RenderAsset *a, NgInstanceBatch *b) {
  // agent: composer-2.5 | 2026-08-09 | instanced draw batch pools | 8837bc
  if (!a || !a->ready || a->shader.handle.id == 0 || !b || b->count <= 0 || !b->mats) {
    return;
  }
  if (a->model.meshCount <= 0) {
    return;
  }
  ng_shader_set_common((NgShader *)&a->shader, (float)GetTime());
  mod_render_set_material_uniforms(a);
  DrawMeshInstanced(a->model.meshes[0], a->model.materials[0], b->mats, b->count);
}

static void mod_render_draw_batch_gbuf(ModRenderCtx *ctx, const RenderAsset *a, NgInstanceBatch *b,
                                      int mode) {
  // agent: composer-2.5 | 2026-08-13 | gbuf no VS vis cull | c3f8a1
  if (!a || !a->ready || !ctx->gbuf_shader_ready || !b || b->count <= 0 || !b->mats) {
    return;
  }
  if (a->model.meshCount <= 0) {
    return;
  }
  Material mat = a->model.materials[0];
  mat.shader = ctx->gbuf_shader.handle;
  mod_render_set_gbuf_uniforms(ctx, a, mode);
  DrawMeshInstanced(a->model.meshes[0], mat, b->mats, b->count);
}

static void mod_render_collect_graph_batches(ModRenderCtx *ctx) {
  mod_render_batches_reset_counts(ctx);
  // agent: composer-2.5 | 2026-08-09 | expire live draw after idle | fa23e5
  // agent: composer-2.5 | 2026-08-13 | CPU batch filter from traverse | 4d378d
  mod_scene_graph_expire_live_draw(GetTime());
  const int n = mod_scene_graph_inst_count();
  const NgRcWsCtx *ws = &ctx->ws_cpu;
  const int skip_cull_filter = ctx->debug_pass == NG_RENDER_PASS_CULLING;
  for (int i = 0; i < n; i++) {
    const NgSceneInst *inst = mod_scene_graph_inst_at(i);
    if (!inst || !inst->model[0]) {
      continue;
    }
    if (!mod_render_asset_for_model(ctx, inst->model)) {
      continue;
    }
    if (!skip_cull_filter && ws->cull_valid && !ng_rc_ws_inst_visible(ws, i)) {
      continue;
    }
    NgInstanceBatch *b = mod_render_batch_get(ctx, inst->model);
    if (!b) {
      continue;
    }
    float pos[3];
    float rot[3];
    if (!mod_scene_graph_sample_draw_pose(inst, GetTime(), mod_scene_graph_interp_delay_s(), pos,
                                          rot)) {
      pos[0] = inst->pos[0];
      pos[1] = inst->pos[1];
      pos[2] = inst->pos[2];
      rot[0] = inst->rot[0];
      rot[1] = inst->rot[1];
      rot[2] = inst->rot[2];
    }
    Matrix m = mod_render_pose_matrix(pos[0], pos[1], pos[2], rot, inst->scale);
    /* Pack graph inst_i in gbuf VS (col3.w). */
    m.m15 = (float)i;
    (void)mod_render_batch_push(b, m);
  }
}

static void mod_render_flush_batches(ModRenderCtx *ctx) {
  for (int i = 0; i < ctx->batch_count; i++) {
    NgInstanceBatch *b = &ctx->batches[i];
    if (b->count <= 0) {
      continue;
    }
    RenderAsset *a = mod_render_cache_get(ctx, b->model);
    if (a) {
      mod_render_draw_batch(a, b);
    }
  }
}

static void mod_render_flush_batches_gbuf(ModRenderCtx *ctx, int mode) {
  for (int i = 0; i < ctx->batch_count; i++) {
    NgInstanceBatch *b = &ctx->batches[i];
    if (b->count <= 0) {
      continue;
    }
    RenderAsset *a = mod_render_cache_get(ctx, b->model);
    if (a) {
      mod_render_draw_batch_gbuf(ctx, a, b, mode);
    }
  }
}

static void mod_render_fill_gbuf_graph(ModRenderCtx *ctx, RenderTexture2D *rt, int mode) {
  // agent: composer-2.5 | 2026-08-10 | gbuf disable blend pack | 8655f8
  /* Alpha blend would multiply world-UVW by dist/FAR → black GI. */
  BeginTextureMode(*rt);
  /* Depth: A=0 empty (BLANK). Other targets: BLACK. */
  ClearBackground(mode == 3 ? BLANK : BLACK);
  rlDisableColorBlend();
  BeginMode3D(ctx->camera);
  mod_render_flush_batches_gbuf(ctx, mode);
  EndMode3D();
  rlEnableColorBlend();
  EndTextureMode();
}

static void mod_render_blit_rt(const RenderTexture2D *rt) {
  /* Use active FBO size so compose/debug work inside the scaled present RT. */
  const float dw = (float)GetRenderWidth();
  const float dh = (float)GetRenderHeight();
  const Rectangle src = {0.0f, 0.0f, (float)rt->texture.width, -(float)rt->texture.height};
  const Rectangle dst = {0.0f, 0.0f, dw, dh};
  DrawTexturePro(rt->texture, src, dst, (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
}

static void mod_render_draw_scene_graph(ModRenderCtx *ctx) {
  // agent: composer-2.5 | 2026-08-09 | instanced draw batch pools | 8837bc
  mod_scene_runtime_use_view();
  mod_render_collect_graph_batches(ctx);
  BeginMode3D(ctx->camera);
  mod_render_flush_batches(ctx);
  EndMode3D();
}

static void mod_render_draw_waiting(ModRenderCtx *ctx) {
  // agent: composer-2.5 | 2026-07-30 | remote waiting status text | 1abeff
  ClearBackground(BLACK);
  char line[192];
  if (mod_render_remote_connecting(line, sizeof(line))) {
    /* connecting at host:port (time) */
  } else if (!mod_net_is_connected()) {
    snprintf(line, sizeof(line), "Starting local play — waiting for the loopback link...");
  } else {
    snprintf(line, sizeof(line), "Waiting for the local world to load...");
  }
  (void)ctx;
  DrawText(line, 20, 20, 18, RAYWHITE);
}

static void mod_render_collect_snapshot_batches(ModRenderCtx *ctx) {
  mod_render_batches_reset_counts(ctx);
  for (int i = 0; i < ctx->curr.entity_count; i++) {
    const NgEntitySnap *e = &ctx->curr.entities[i];
    const NgEntitySnap *p = e;
    if (ctx->have_prev) {
      for (int j = 0; j < ctx->prev.entity_count; j++) {
        if (ctx->prev.entities[j].id == e->id) {
          p = &ctx->prev.entities[j];
          break;
        }
      }
    }
    const NgSceneMeshKind kind =
        e->type == NG_ENTITY_SPHERE ? NG_SCENE_MESH_SPHERE : NG_SCENE_MESH_CUBE;
    char key[16];
    snprintf(key, sizeof(key), "@%d", (int)kind);
    if (!mod_render_asset_for_mesh_kind(ctx, kind)) {
      continue;
    }
    NgInstanceBatch *b = mod_render_batch_get(ctx, key);
    if (!b) {
      continue;
    }
    const float yaw = mod_render_lerp(p ? p->rot_y : e->rot_y, e->rot_y, ctx->alpha);
    const float pos[3] = {mod_render_lerp(p ? p->pos[0] : e->pos[0], e->pos[0], ctx->alpha),
                          mod_render_lerp(p ? p->pos[1] : e->pos[1], e->pos[1], ctx->alpha),
                          mod_render_lerp(p ? p->pos[2] : e->pos[2], e->pos[2], ctx->alpha)};
    const float rot[3] = {0.0f, yaw, 0.0f};
    (void)mod_render_batch_push(b, mod_render_pose_matrix(pos[0], pos[1], pos[2], rot, 1.0f));
  }
}

static void mod_render_draw_snapshot(ModRenderCtx *ctx) {
  mod_render_collect_snapshot_batches(ctx);
  BeginMode3D(ctx->camera);
  for (int i = 0; i < ctx->batch_count; i++) {
    NgInstanceBatch *b = &ctx->batches[i];
    if (b->count <= 0) {
      continue;
    }
    RenderAsset *a = mod_render_cache_get(ctx, b->model);
    if (a) {
      mod_render_draw_batch(a, b);
    }
  }
  EndMode3D();
}

static void mod_render_draw_scene(ModRenderCtx *ctx) {
  ClearBackground(BLACK);
  mod_render_update_camera(ctx);

  if (!mod_render_ensure_present(ctx)) {
    mod_render_draw_overlay(mod_render_authoritative_label(ctx), 10);
    return;
  }

  mod_scene_runtime_use_view();
  const bool graph = mod_scene_view_graph_active() ||
                     (mod_scene_view_is_loaded() && mod_scene_graph_inst_count() > 0) ||
                     (mod_net_is_authoritative() && mod_scene_is_loaded());

  // agent: composer-2.5 | 2026-08-09 | gate RC by scene render mode | 2a6d5a
  // agent: composer-2.5 | 2026-08-09 | Phase2 GPU FBO RC path | 5642b8
  const NgSceneViewMeta *vmeta = mod_scene_assets_view();
  const NgSceneRenderMode rmode =
      (vmeta && vmeta->valid) ? vmeta->render_mode : NG_SCENE_RENDER_SIMPLE;
  const bool feature_gbuf =
      rmode == NG_SCENE_RENDER_GBUFFER || rmode == NG_SCENE_RENDER_RC;
  const bool feature_rc = rmode == NG_SCENE_RENDER_RC;
  const bool want_rc = graph && feature_rc;
  const bool want_gbuf =
      graph && feature_gbuf && (want_rc || ctx->debug_pass != NG_RENDER_PASS_FINAL);

  /* Fill offscreen gbuf/cascades first. Raylib EndTextureMode returns to the
   * default FB — do not nest those fills inside the present RT. */
  // agent: composer-2.5 | 2026-08-09 | fix present FBO nesting | c075d5
  bool composed = false;
  if (want_gbuf) {
    if (mod_net_is_authoritative() && !mod_scene_view_graph_active() && mod_scene_is_loaded()) {
      mod_scene_runtime_use_server();
    } else {
      mod_scene_runtime_use_view();
    }
    if (mod_render_ensure_gbuf(ctx)) {
#if NG_RC_WS_GI_OFFLINE
      /* Foundation: no look-at clip cube — gbuf world XYZ + camera frustum cull. */
      // agent: composer-2.5 | 2026-08-11 | wipe look-at cube cull path | bf3c1d
      // agent: composer-2.5 | 2026-08-12 | gbuf after cull probes order | 9929da
#else
      /* Frustum volume must match gbuf UVW packing this frame. */
      // agent: composer-2.5 | 2026-08-10 | frustum before gbuf CPU stamp | fff863
      mod_render_rc_ws_update_frustum_aabb(ctx);
#endif
      /* Hot path: cull → probes(prev vis) → gbuf VS cull → swap vis. */
      // agent: composer-2.5 | 2026-08-12 | VS cull double-buffer vis | aac081
      bool rc_ready = false;
      if (want_rc && mod_render_ensure_rc(ctx)) {
        mod_render_rc_gpu_tick(ctx);
        rc_ready = ctx->rc_rt_ready && ctx->ws_rt_ready && ctx->rc_compose.ready &&
                   ctx->ws_cpu.prim_tex_ready && ctx->ws_cpu.bvh_tex_ready && ctx->ws_cull_ready;
      }
      if (rc_ready) {
        mod_render_rc_ws_probe_tick(ctx);
        if (ctx->ws_rt_ready) {
          const int ping = ctx->ws_ping & 1;
          BeginTextureMode(ctx->rt_ws[ping]);
          ClearBackground(BLACK);
          EndTextureMode();
        }
      }
      mod_render_collect_graph_batches(ctx);
      mod_render_fill_gbuf_graph(ctx, &ctx->rt_albedo, 0);
      mod_render_fill_gbuf_graph(ctx, &ctx->rt_normal, 1);
      mod_render_fill_gbuf_graph(ctx, &ctx->rt_glow, 2);
      mod_render_fill_gbuf_graph(ctx, &ctx->rt_depth, 3);
      mod_render_fill_gbuf_graph(ctx, &ctx->rt_prim_id, 4);
      mod_render_fill_gbuf_graph(ctx, &ctx->rt_probe_id, 5);
      if (rc_ready) {
        mod_render_rc_ws_id_tick(ctx);
        mod_render_rc_ws_vis_swap(ctx);
      }

      if (ctx->debug_pass != NG_RENDER_PASS_FINAL || want_rc) {
        BeginTextureMode(ctx->rt_present);
        if (ctx->debug_pass == NG_RENDER_PASS_ALBEDO) {
          ClearBackground(BLACK);
          mod_render_blit_rt(&ctx->rt_albedo);
        } else if (ctx->debug_pass == NG_RENDER_PASS_NORMAL) {
          ClearBackground(BLACK);
          mod_render_blit_rt(&ctx->rt_normal);
        } else if (ctx->debug_pass == NG_RENDER_PASS_GLOW) {
          ClearBackground(BLACK);
          mod_render_blit_rt(&ctx->rt_glow);
        } else if (ctx->debug_pass == NG_RENDER_PASS_DEPTH) {
          ClearBackground(BLACK);
          mod_render_blit_rt(&ctx->rt_depth);
        } else if (ctx->debug_pass == NG_RENDER_PASS_IRRADIANCE && rc_ready) {
          ClearBackground(BLACK);
          mod_render_rc_ws_view(ctx);
        // agent: composer-2.5 | 2026-08-10 | debug probes grid atlas wire | 49402d
        } else if (ctx->debug_pass == NG_RENDER_PASS_UVW) {
          ClearBackground(BLACK);
          if (ctx->rc_ws_debug.ready) {
            mod_render_rc_ws_debug(ctx, 1);
          } else {
            mod_render_blit_rt(&ctx->rt_depth);
          }
        } else if (ctx->debug_pass == NG_RENDER_PASS_PROBES && rc_ready) {
          ClearBackground(BLACK);
          mod_render_rc_ws_debug(ctx, 0);
        // agent: composer-2.5 | 2026-08-12 | add probes-lod debug pass | b65b50
        } else if (ctx->debug_pass == NG_RENDER_PASS_PROBES_LOD && rc_ready) {
          ClearBackground(BLACK);
          mod_render_rc_ws_debug(ctx, 4);
        } else if (ctx->debug_pass == NG_RENDER_PASS_GRID && rc_ready) {
          ClearBackground(BLACK);
          mod_render_rc_ws_debug(ctx, 2);
        } else if (ctx->debug_pass == NG_RENDER_PASS_ATLAS && rc_ready) {
          ClearBackground(BLACK);
          mod_render_blit_rt(&ctx->ws_clip[NG_RC_WS_CLIP_NEAR].casc[0]);
        } else if (ctx->debug_pass == NG_RENDER_PASS_CULLING && rc_ready) {
          // agent: composer-2.5 | 2026-08-11 | BVH frustum cull foundation wire | 868ee4
          ClearBackground(BLACK);
          mod_render_rc_ws_debug(ctx, 3);
        } else if (rc_ready) {
          ClearBackground(mod_render_bg_color());
          mod_render_rc_compose(ctx);
        } else {
          ClearBackground(BLACK);
          mod_render_blit_rt(&ctx->rt_albedo);
        }
        EndTextureMode();
        composed = true;
      }
    }
  }

  if (!composed) {
    BeginTextureMode(ctx->rt_present);
    ClearBackground(mod_render_bg_color());
    if (mod_scene_view_graph_active()) {
      mod_render_draw_scene_graph(ctx);
    } else if (ctx->have_curr && ctx->curr.entity_count > 0) {
      mod_render_draw_snapshot(ctx);
    } else if (mod_scene_view_is_loaded()) {
      mod_render_draw_scene_graph(ctx);
    } else if (mod_net_is_authoritative() && mod_scene_is_loaded()) {
      mod_scene_runtime_use_server();
      mod_render_draw_scene_graph(ctx);
    } else if (ctx->have_curr) {
      mod_render_draw_snapshot(ctx);
    }
    EndTextureMode();
  }

  mod_render_present_to_screen(ctx);
  mod_render_draw_overlay(mod_render_authoritative_label(ctx), 10);
  DrawText(TextFormat("scale=%.2f %dx%d", ctx->render_scale, ctx->present_w, ctx->present_h), 10,
           34, 18, LIME);
}
// agent: composer-2.5 | 2026-07-26 | session bootstrap render state | d8e9f0
void mod_render_apply_session(const NgSessionState *session) {
  ModRenderCtx *ctx = &g_render_ctx;
  if (!session) {
    return;
  }
  strncpy(ctx->scene_label, session->scene_id, sizeof(ctx->scene_label) - 1);
  ctx->scene_label[sizeof(ctx->scene_label) - 1] = '\0';
  mod_render_clear_cache(ctx);
  ctx->have_session = true;
  ctx->scene_sync = session->scene_sync;
}

void mod_render_apply_action(const NgActionResult *result) {
  ModRenderCtx *ctx = &g_render_ctx;
  if (!result) {
    return;
  }
  if (result->have_state) {
    ctx->prev = ctx->curr;
    ctx->curr = result->state;
    ctx->have_prev = ctx->have_curr;
    ctx->have_curr = true;
    ctx->alpha = 0.0f;
    mod_scene_runtime_use_view();
    const char *view_id = mod_scene_view_current_id();
    if (!mod_scene_view_is_loaded() || !view_id || view_id[0] == '\0' ||
        strcmp(view_id, result->state.scene_id) == 0) {
      strncpy(ctx->scene_label, result->state.scene_id, sizeof(ctx->scene_label) - 1);
      ctx->scene_label[sizeof(ctx->scene_label) - 1] = '\0';
    }
  } else if (result->reply[0] != '\0') {
    /* reply-only action (e.g. agent snapshot query) */
  }
}

static bool mod_render_on_msg(const NgMsg *msg, void *vctx) {
  ModRenderCtx *ctx = (ModRenderCtx *)vctx;
  if (!ctx || !msg) {
    return false;
  }

  switch (msg->kind) {
  case NG_MSG_SNAPSHOT:
    if (msg->snapshot) {
      ctx->prev = ctx->curr;
      ctx->curr = *msg->snapshot;
      ctx->have_prev = ctx->have_curr;
      ctx->have_curr = true;
      ctx->alpha = 0.0f;
      mod_scene_runtime_use_view();
      const char *view_id = mod_scene_view_current_id();
      if (!mod_scene_view_is_loaded() || !view_id || view_id[0] == '\0' ||
          strcmp(view_id, msg->snapshot->scene_id) == 0) {
        strncpy(ctx->scene_label, msg->snapshot->scene_id, sizeof(ctx->scene_label) - 1);
        ctx->scene_label[sizeof(ctx->scene_label) - 1] = '\0';
      }
    }
    return true;
  case NG_MSG_EVENT:
    if (msg->text) {
      strncpy(ctx->scene_label, msg->text, sizeof(ctx->scene_label) - 1);
    }
    return true;
  case NG_MSG_TICK:
    if (ctx->have_curr) {
      ctx->alpha += msg->dt * 20.0f;
      if (ctx->alpha > 1.0f) {
        ctx->alpha = 1.0f;
      }
    }
    return true;
  case NG_MSG_DRAW:
    // Prefer waiting UI while a remote join has not received a view yet.
    if (mod_render_remote_connecting(NULL, 0)) {
      mod_render_draw_waiting(ctx);
    } else if (ctx->have_curr || ctx->have_session || mod_scene_view_is_loaded() ||
               (mod_net_is_authoritative() && mod_scene_is_loaded())) {
      mod_render_draw_scene(ctx);
    } else {
      mod_render_draw_waiting(ctx);
    }
    return true;
  default:
    return false;
  }
}

static bool mod_render_init(void *vctx) {
  ModRenderCtx *ctx = (ModRenderCtx *)vctx;
  memset(ctx, 0, sizeof(*ctx));
  ctx->scene_label[0] = '\0';
  ctx->debug_pass = NG_RENDER_PASS_FINAL;
  // agent: composer-2.5 | 2026-08-09 | Phase2 GPU FBO RC path | 5642b8
  // agent: composer-2.5 | 2026-08-09 | WS then SS pipeline wire | ed5dfb
  ctx->rc_quality = 2;
  // agent: composer-2.5 | 2026-08-10 | gi_strength default 1.0 again | 95a834
  ctx->gi_strength = 1.0f;
  // agent: composer-2.5 | 2026-08-10 | ws ss weight defaults | 1b1432
  ctx->ws_weight = 1.0f;
  // agent: composer-2.5 | 2026-08-10 | ss_weight 0 skip SS tick | 55e85e
  ctx->ss_weight = 0.0f;
  ctx->render_scale = 1.0f;
#if !NG_RC_WS_GI_OFFLINE
  // agent: composer-2.5 | 2026-08-10 | B1 dual clip near far RTs | d8ac7a
  {
    const float near_e = NG_RC_WS_CELL * (float)NG_RC_WS_VOX_RES;
    const float far_e = near_e * 2.0f;
    NgRcWsClip *near = &ctx->ws_clip[NG_RC_WS_CLIP_NEAR];
    NgRcWsClip *far = &ctx->ws_clip[NG_RC_WS_CLIP_FAR];
    near->origin[0] = -near_e * 0.5f;
    near->origin[1] = -near_e * 0.5f;
    near->origin[2] = -near_e * 0.5f;
    near->size[0] = near_e;
    near->size[1] = near_e;
    near->size[2] = near_e;
    far->origin[0] = -far_e * 0.5f;
    far->origin[1] = -far_e * 0.5f;
    far->origin[2] = -far_e * 0.5f;
    far->size[0] = far_e;
    far->size[1] = far_e;
    far->size[2] = far_e;
  }
#endif
  ng_rc_ws_init(&ctx->ws_cpu);
#if !NG_RC_WS_GI_OFFLINE
  // agent: composer-2.5 | 2026-08-11 | skip clip sync init GI off | db7a00
  ctx->ws_cpu.origin[0] = ctx->ws_clip[NG_RC_WS_CLIP_FAR].origin[0];
  ctx->ws_cpu.origin[1] = ctx->ws_clip[NG_RC_WS_CLIP_FAR].origin[1];
  ctx->ws_cpu.origin[2] = ctx->ws_clip[NG_RC_WS_CLIP_FAR].origin[2];
  ctx->ws_cpu.size[0] = ctx->ws_clip[NG_RC_WS_CLIP_FAR].size[0];
  ctx->ws_cpu.size[1] = ctx->ws_clip[NG_RC_WS_CLIP_FAR].size[1];
  ctx->ws_cpu.size[2] = ctx->ws_clip[NG_RC_WS_CLIP_FAR].size[2];
#endif
  mod_render_init_camera(ctx);
  return true;
}

static void mod_render_shutdown(void *vctx) {
  ModRenderCtx *ctx = (ModRenderCtx *)vctx;
  mod_render_clear_cache(ctx);
  mod_render_unload_gbuf(ctx);
  mod_render_unload_rc(ctx);
  mod_render_unload_present(ctx);
}

// agent: composer-2.5 | 2026-07-29 | Extend NgModOps side fixed_step | 220dba
static const NgModOps g_render_ops = {
    .name = "render",
    .dest = NG_BUS_RENDER,
    .side = NG_MOD_SIDE_CLIENT,
    .init = mod_render_init,
    .shutdown = mod_render_shutdown,
    .on_msg = mod_render_on_msg,
    .fixed_step = NULL,
};

const NgModOps *mod_render_ops(void) { return &g_render_ops; }

void *mod_render_ctx(void) { return &g_render_ctx; }

bool mod_render_has_snapshot(void) {
  return g_render_ctx.have_curr || g_render_ctx.have_session || mod_scene_view_is_loaded() ||
         (mod_net_is_authoritative() && mod_scene_is_loaded());
}

void mod_render_snapshot_text(char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  const ModRenderCtx *ctx = &g_render_ctx;
  mod_scene_runtime_use_view();
  const char *view_scene = mod_scene_view_current_id();
  const int view_graph = mod_scene_graph_inst_count();
  const int view_entities = mod_scene_view_entity_count();
  const char *label =
      (view_scene && view_scene[0] != '\0') ? view_scene
                                            : (ctx->scene_label[0] != '\0' ? ctx->scene_label : "?");
  snprintf(out, cap, "render scene=%s snapshot=%d graph=%d entities=%d", label,
           ctx->have_curr ? ctx->curr.entity_count : 0, view_graph, view_entities);
}

/** MCP: used/free/headroom + lod hist from GPU stats + slots (rare readback). */
void mod_render_probe_snapshot_text(char *out, size_t cap) {
  // agent: grok-4.6 | 2026-08-12 | probe snapshot MCP text | 73bc00
  if (!out || cap == 0) {
    return;
  }
  ModRenderCtx *ctx = &g_render_ctx;
  if (!ctx->ws_probe_ready || !ctx->probe_gpu_valid || ctx->rt_probe_stats.id == 0) {
    snprintf(out, cap, "probe ready=0");
    return;
  }
  const int budget = ctx->ws_cpu.slot_cap / 2;
  int used_n = 0;
  int free_n = ctx->ws_cpu.slot_cap;
  Image img = LoadImageFromTexture(ctx->rt_probe_stats.texture);
  if (img.data && img.width >= 1 && img.height >= 1) {
    const float *px = (const float *)img.data;
    used_n = (int)(px[0] + 0.5f);
    free_n = (int)(px[1] + 0.5f);
  }
  UnloadImage(img);
  int headroom = budget - used_n;
  if (headroom < 0) {
    headroom = 0;
  }
  int hist[NG_RC_WS_LOD_SOFT_MAX + 1];
  memset(hist, 0, sizeof(hist));
  int lmin = 99;
  int lmax = -1;
  if (ctx->rt_probe_slots.id != 0) {
    Image slots = LoadImageFromTexture(ctx->rt_probe_slots.texture);
    if (slots.data && slots.width > 0) {
      const float *px = (const float *)slots.data;
      const int n = slots.width < ctx->ws_cpu.slot_cap ? slots.width : ctx->ws_cpu.slot_cap;
      for (int i = 0; i < n; i++) {
        const float a = px[i * 4 + 3];
        if (a < 0.5f) {
          continue;
        }
        int lod = (int)(a + 0.5f) - 1;
        if (lod < 0) {
          lod = 0;
        } else if (lod > NG_RC_WS_LOD_SOFT_MAX) {
          lod = NG_RC_WS_LOD_SOFT_MAX;
        }
        hist[lod]++;
        if (lod < lmin) {
          lmin = lod;
        }
        if (lod > lmax) {
          lmax = lod;
        }
      }
    }
    UnloadImage(slots);
  }
  size_t n = (size_t)snprintf(out, cap,
                              "probe used=%d free=%d headroom=%d budget=%d/%d min=%d max=%d", used_n,
                              free_n, headroom, budget, ctx->ws_cpu.slot_cap, lmin, lmax);
  for (int L = NG_RC_WS_LOD_SOFT_MAX; L >= 0 && n + 12 < cap; L--) {
    if (hist[L] <= 0) {
      continue;
    }
    n += (size_t)snprintf(out + n, cap - n, " L%d=%d", L, hist[L]);
  }
}

void mod_render_visibility_text(char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  mod_scene_runtime_use_view();
  const NgSceneViewMeta *view = mod_scene_assets_view();
  const ModRenderCtx *ctx = &g_render_ctx;
  const int graph = mod_scene_graph_inst_count();
  const int snap = ctx->have_curr ? ctx->curr.entity_count : 0;
  const int visible = graph > 0 ? graph : snap;
  if (view) {
    snprintf(out, cap, "visible=%d bg=%02x%02x%02x loaded=%d", visible, view->bg_r, view->bg_g,
             view->bg_b, mod_scene_view_is_loaded() ? 1 : 0);
  } else {
    snprintf(out, cap, "visible=%d bg=000000 loaded=%d", visible,
             mod_scene_view_is_loaded() ? 1 : 0);
  }
}

bool mod_render_set(const char *path, const char *value) {
  if (!path || !value) {
    return false;
  }
  // agent: composer-2.5 | 2026-08-09 | CLI set render.rc tree | 28de10
  // agent: composer-2.5 | 2026-08-09 | pass under debug.render | c088d1
  if (strcmp(path, "debug.render.pass") == 0) {
    NgRenderDebugPass pass;
    if (!mod_render_pass_from_name(value, &pass)) {
      return false;
    }
    g_render_ctx.debug_pass = pass;
    return true;
  }
  // agent: composer-2.5 | 2026-08-13 | opt-in probe occupancy log | f07898
  if (strcmp(path, "debug.logging.probes") == 0) {
    if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "on") == 0) {
      g_render_ctx.debug_logging_probes = true;
      return true;
    }
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 || strcmp(value, "off") == 0) {
      g_render_ctx.debug_logging_probes = false;
      return true;
    }
    return false;
  }
  if (strcmp(path, "render.rc.quality") == 0) {
    const int q = atoi(value);
    if (q < 0 || q > 4) {
      return false;
    }
    g_render_ctx.rc_quality = q;
    return true;
  }
  if (strcmp(path, "render.rc.gi_strength") == 0) {
    const float g = (float)atof(value);
    if (g < 0.0f || g > 8.0f) {
      return false;
    }
    g_render_ctx.gi_strength = g;
    return true;
  }
  if (strcmp(path, "render.scale") == 0) {
    const float s = (float)atof(value);
    if (s < 0.25f || s > 2.0f) {
      return false;
    }
    g_render_ctx.render_scale = s;
    /* Recreate all scaled RTs next frame. */
    mod_render_unload_gbuf(&g_render_ctx);
    mod_render_unload_rc(&g_render_ctx);
    mod_render_unload_present(&g_render_ctx);
    return true;
  }
  return false;
}

bool mod_render_get(const char *path, char *out, size_t cap) {
  if (!path || !out || cap == 0) {
    return false;
  }
  if (strcmp(path, "debug.render.pass") == 0) {
    snprintf(out, cap, "%s", mod_render_pass_name(g_render_ctx.debug_pass));
    return true;
  }
  // agent: composer-2.5 | 2026-08-13 | opt-in probe occupancy log | f07898
  if (strcmp(path, "debug.logging.probes") == 0) {
    snprintf(out, cap, "%d", g_render_ctx.debug_logging_probes ? 1 : 0);
    return true;
  }
  if (strcmp(path, "render.rc.quality") == 0) {
    snprintf(out, cap, "%d", g_render_ctx.rc_quality);
    return true;
  }
  if (strcmp(path, "render.rc.gi_strength") == 0) {
    snprintf(out, cap, "%.2f", g_render_ctx.gi_strength);
    return true;
  }
  if (strcmp(path, "render.scale") == 0) {
    snprintf(out, cap, "%.2f", g_render_ctx.render_scale);
    return true;
  }
  return false;
}

// agent: composer-2.5 | 2026-07-29 | snapshot before empty graph | 57ca7b
// agent: composer-2.5 | 2026-07-29 | overlay label view authority | 6d7863
// agent: composer-2.5 | 2026-07-28 | render drop embedded path | f42f1c
// agent: composer-2.5 | 2026-07-29 | draw entities via model transform | 1415d8
// agent: composer-2.5 | 2026-07-29 | Extend NgModOps side fixed_step | 220dba
// agent: composer-2.5 | 2026-07-30 | remote waiting status text | 1abeff
// agent: composer-2.5 | 2026-07-30 | overlay connecting status | 0d072a
// agent: composer-2.5 | 2026-07-30 | render pose vel extrapolate | 25c348
// agent: composer-2.5 | 2026-07-30 | render hermite state samples | f452ba
// agent: composer-2.5 | 2026-08-01 | adaptive interp delay API | 5b890f
// agent: composer-2.5 | 2026-08-02 | draw via quat not RotateXYZ | 838826
// agent: composer-2.5 | 2026-08-09 | shader glow rough metal uniforms | 7e0b28
// agent: composer-2.5 | 2026-08-09 | gbuffer RTs debug blit | 96d6a0
// agent: composer-2.5 | 2026-08-09 | instanced draw batch pools | 8837bc
// agent: composer-2.5 | 2026-08-09 | Phase2 SS RC render path | ff1b7f
// agent: composer-2.5 | 2026-08-09 | gate RC by scene render mode | 2a6d5a
// agent: composer-2.5 | 2026-08-09 | gi_strength CLI render | a5a86e
// agent: composer-2.5 | 2026-08-09 | default gi_strength 1.0 | 6674ef
// agent: composer-2.5 | 2026-08-09 | expire live draw after idle | 4e7ce8
// agent: composer-2.5 | 2026-08-09 | CLI set render.rc tree | 28de10
// agent: composer-2.5 | 2026-08-09 | pass under debug.render | c088d1
// agent: composer-2.5 | 2026-08-09 | present RT for render.scale | 2c679e
// agent: composer-2.5 | 2026-08-09 | fix present FBO nesting | c075d5
// agent: composer-2.5 | 2026-08-09 | wire WS RC quality path | d68d00
// agent: composer-2.5 | 2026-08-09 | uniform WS quality cost scale | 45673f
// agent: composer-2.5 | 2026-08-09 | Phase2 GPU FBO RC path | 5642b8
// agent: composer-2.5 | 2026-08-09 | WS then SS pipeline wire | ed5dfb
// agent: composer-2.5 | 2026-08-09 | stamp seed prop butter order | b9a6eb
// agent: composer-2.5 | 2026-08-09 | XZ splat emitters-only stamp | bd73fb
// agent: composer-2.5 | 2026-08-09 | WS fs draw no Y flip | 07886c
// agent: composer-2.5 | 2026-08-09 | wire WS radial RC | 18204b
// agent: composer-2.5 | 2026-08-09 | half-res WS casc bilinear merge | 187201
// agent: composer-2.5 | 2026-08-09 | WS blit use raylib Y flip | 7f49b1
// agent: composer-2.5 | 2026-08-09 | wire SS packed RC | 7ba28e
// agent: composer-2.5 | 2026-08-10 | RC cost shift dirs cut | d9bceb
// agent: composer-2.5 | 2026-08-10 | wire WS volume RC tick | 0fd9cd
// agent: composer-2.5 | 2026-08-10 | WS atlas nearest no Vflip | 787633
// agent: composer-2.5 | 2026-08-10 | WS blit identity no flip | b8e489
// agent: composer-2.5 | 2026-08-10 | depth far WS simplify | 6dd473
// agent: composer-2.5 | 2026-08-10 | ss weight zero isolate WS | b458b3
// agent: composer-2.5 | 2026-08-10 | negate cam_right reconstruct | 95d5a9
// agent: composer-2.5 | 2026-08-10 | wire ws origin gbuf world | 4bc5ef
// agent: composer-2.5 | 2026-08-10 | gbuf disable blend pack | 8655f8
// agent: composer-2.5 | 2026-08-10 | SS fill world dist uniforms | 9b6064
// agent: composer-2.5 | 2026-08-10 | bind irr before glow | e53155
// agent: composer-2.5 | 2026-08-10 | butter shader copy no blit | 4e485a
// agent: composer-2.5 | 2026-08-10 | ws ss weight defaults | 1b1432
// agent: composer-2.5 | 2026-08-10 | SS fill no glow hits | 94de82
// agent: composer-2.5 | 2026-08-10 | compose drop glow term | 778f4d
// agent: composer-2.5 | 2026-08-10 | ss weight modest default | 63fc4f
// agent: composer-2.5 | 2026-08-10 | ss_weight 0 skip SS tick | 55e85e
// agent: composer-2.5 | 2026-08-10 | gi_strength default 1.0 again | 95a834
// agent: composer-2.5 | 2026-08-10 | true WS RC tick cascade chain | 82877c
// agent: composer-2.5 | 2026-08-10 | wire SH encode after merge | 57cf52

// agent: composer-2.5 | 2026-08-10 | frustum AABB GPU vox tick | e8fa02
// agent: composer-2.5 | 2026-08-10 | frustum before gbuf CPU stamp | fff863
// agent: composer-2.5 | 2026-08-10 | world-snap fixed cell GI volume | 69d77e
// agent: composer-2.5 | 2026-08-10 | target-center GI clip volume | b97038
// agent: composer-2.5 | 2026-08-10 | frustum AABB coverage clip | 6e1150
// agent: composer-2.5 | 2026-08-10 | fixed cube cam-biased clip | c96f11
// agent: composer-2.5 | 2026-08-10 | look-at lock clip no cam follow | 66d9b3
// agent: composer-2.5 | 2026-08-10 | compose view use present size | 316071
// agent: composer-2.5 | 2026-08-10 | tick comment rc-ws 6.2 note | 3a1e8d
// agent: composer-2.5 | 2026-08-10 | tick bind rebuild_prims | c31da5
// agent: composer-2.5 | 2026-08-10 | tick skip rebuild_vox | bb5602
// agent: composer-2.5 | 2026-08-10 | tick cascade interval tune | 7c0ad2
// agent: composer-2.5 | 2026-08-10 | compose bind cam for specular | d19ab3
// agent: composer-2.5 | 2026-08-10 | bind tex_grid fill tick | 820804
// agent: composer-2.5 | 2026-08-10 | debug probes grid atlas wire | 49402d
// agent: composer-2.5 | 2026-08-10 | B1 dual clip near far RTs | d8ac7a
// agent: composer-2.5 | 2026-08-10 | B2 amortize far fill cadence | e09d1a
// agent: composer-2.5 | 2026-08-10 | shared near cell intervals fill | e61290
// agent: composer-2.5 | 2026-08-10 | CELL meter intervals no far gain | 1f82f9
// agent: composer-2.5 | 2026-08-10 | lockstep clip scroll soft blend | 23e626
// agent: composer-2.5 | 2026-08-10 | B3 sparse tick seed fill | 4d7b85
// agent: composer-2.5 | 2026-08-11 | B6 kill seed readback tick | 7c8edc
// agent: composer-2.5 | 2026-08-11 | B62 skip fill if no dirty | bcd897
// agent: composer-2.5 | 2026-08-11 | B64 wire GPU prio passes | f7b538
// agent: composer-2.5 | 2026-08-10 | B3 seed blit quiet readback | 034eb0
// agent: composer-2.5 | 2026-08-10 | B3 fill skip clear dirty only | 93345e
// agent: composer-2.5 | 2026-08-10 | ws fill disable blend dirty | 466455
// agent: composer-2.5 | 2026-08-10 | merge pingpong not casc | 27d0fe
// agent: composer-2.5 | 2026-08-10 | LOD bands from camera eye | 4e0c43
// agent: composer-2.5 | 2026-08-11 | B65 wire set_view before tick | 9d1e74
// agent: composer-2.5 | 2026-08-11 | BVH frustum cull foundation wire | 868ee4
// agent: composer-2.5 | 2026-08-11 | fix cull debug tex unit bind | a5a0fc
// agent: composer-2.5 | 2026-08-11 | flush clear sampler slots cull | 53b39b
// agent: composer-2.5 | 2026-08-11 | restore GPU cull fix frustum | 15ce06
// agent: composer-2.5 | 2026-08-11 | camera-basis GPU frustum planes | 749a9e
// agent: composer-2.5 | 2026-08-11 | culling debug bind flush units | af8b89
// agent: composer-2.5 | 2026-08-11 | culling debug bind all samplers | 6e7512
// agent: composer-2.5 | 2026-08-11 | frustum side normals via cross | 5bf367
// agent: composer-2.5 | 2026-08-11 | cull log visible prim mask | 0bb751
// agent: composer-2.5 | 2026-08-11 | VP frustum planes target orient | bae74f
// agent: composer-2.5 | 2026-08-11 | culling debug bind tex_prim | 43c334
// agent: composer-2.5 | 2026-08-11 | cull log until eight vis | 892ba3
// agent: composer-2.5 | 2026-08-11 | wire depth extent uniforms | 8ff0b2
// agent: composer-2.5 | 2026-08-11 | wipe look-at cube cull path | bf3c1d
// agent: composer-2.5 | 2026-08-11 | skip clip grid when GI off | 8bfa78
// agent: composer-2.5 | 2026-08-11 | skip clip sync init GI off | db7a00
// agent: composer-2.5 | 2026-08-11 | vox dirty scene hash only | ec6769
// agent: composer-2.5 | 2026-08-11 | disable cull GPU TraceLog | 413ecd
// agent: composer-2.5 | 2026-08-11 | wire surface tick after cull | 3a9cde
// agent: composer-2.5 | 2026-08-12 | deferred prim probe id passes | f9cfb4
// agent: composer-2.5 | 2026-08-12 | add probes-lod debug pass | b65b50
// agent: composer-2.5 | 2026-08-12 | VS cull double-buffer vis | aac081
// agent: composer-2.5 | 2026-08-12 | persistent probe fingerprint | 00025d
// agent: composer-2.5 | 2026-08-12 | camera-basis inward frustum planes | 8e0518
// agent: composer-2.5 | 2026-08-12 | frustum per-plane target orient | 8a11d4
// agent: composer-2.5 | 2026-08-12 | GPU prim cull expand restore | 0dcbca
// agent: composer-2.5 | 2026-08-12 | VS cull material map bind | 1fd856
// agent: composer-2.5 | 2026-08-12 | GPU probe residency RTs | 4b745d
// agent: composer-2.5 | 2026-08-12 | GPU probe tick no CPU | 7befcb
// agent: composer-2.5 | 2026-08-12 | probe tick split-merge wire | 3bc02b
// agent: composer-2.5 | 2026-08-12 | slots ping-pong for split | f33dd4
// agent: composer-2.5 | 2026-08-12 | probe tick single-root wire | 70b763
// agent: composer-2.5 | 2026-08-12 | coarse AABB fine shell keep | 9119d3
// agent: composer-2.5 | 2026-08-12 | probe tick root-only no gens | 84271d
// agent: composer-2.5 | 2026-08-12 | probe tick cover lod uniform | c6f2be
// agent: composer-2.5 | 2026-08-12 | clarify Gen0 flood log | 982f1d
// agent: composer-2.5 | 2026-08-12 | Gen0 log cells over cap | 8c58ae
// agent: composer-2.5 | 2026-08-12 | probe tick restore gen waves | d29207
// agent: composer-2.5 | 2026-08-12 | probe incremental residency tick | 5af26a
// agent: composer-2.5 | 2026-08-12 | lazy stochastic split steal | 1c5864
// agent: grok-4.6 | 2026-08-12 | steal then even split log | b6ca16
// agent: grok-4.6 | 2026-08-12 | cover pass budget uniform | eaf197
// agent: grok-4.6 | 2026-08-12 | split then steal skip compact | 680d06
// agent: grok-4.6 | 2026-08-12 | probe snapshot MCP text | 73bc00
// agent: grok-4.6 | 2026-08-12 | steal cover-pressure uniform | 21c74e
// agent: grok-4.6 | 2026-08-12 | persist probe tick GPU | cabbaa
// agent: composer-2.5 | 2026-08-12 | GPU probe tick CPU order | c1f502
// agent: composer-2.5 | 2026-08-12 | GPU cover after relax pass | ad8ebb
// agent: composer-2.5 | 2026-08-13 | CPU cull tick upload vis | 4d378d
// agent: composer-2.5 | 2026-08-13 | gbuf no VS vis cull | c3f8a1
