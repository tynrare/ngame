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
  /* WS true RC: dir-packed cascade atlases + L1 SH + frustum GPU vox. */
  // agent: composer-2.5 | 2026-08-10 | frustum AABB GPU vox tick | e8fa02
  RenderTexture2D rt_ws_casc[NG_RC_CASCADES_MAX];
  RenderTexture2D rt_ws_stamp;
  RenderTexture2D rt_ws_sh;
  RenderTexture2D rt_ws[2];
  RenderTexture2D rt_vox; /* VOX × VOX² frustum-fit stamp atlas */
  bool vox_rt_ready;
  float ws_snap_origin[3];
  float ws_snap_size[3];
  uint32_t ws_vox_scene_hash;
  int ws_ping;
  bool ws_rt_ready;
  int ws_probe_n;
  int ws_dirs;
  int ws_irr_w;
  int ws_irr_h;
  float ws_origin[3];
  float ws_size[3];
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
  NgRcPassShader rc_ws_vox_stamp;
} ModRenderCtx;

static ModRenderCtx g_render_ctx;
static const Vector3 NG_RC_SKY = {0.08f, 0.10f, 0.14f};

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
  {
    int loc = GetShaderLocation(sh->handle, "ng_ws_origin");
    if (loc >= 0) {
      SetShaderValue(sh->handle, loc, ctx->ws_origin, SHADER_UNIFORM_VEC3);
    }
    loc = GetShaderLocation(sh->handle, "ng_ws_size");
    if (loc >= 0) {
      SetShaderValue(sh->handle, loc, ctx->ws_size, SHADER_UNIFORM_VEC3);
    }
  }
}

static void mod_render_unload_gbuf(ModRenderCtx *ctx) {
  if (ctx->gbuf_ready) {
    UnloadRenderTexture(ctx->rt_albedo);
    UnloadRenderTexture(ctx->rt_normal);
    UnloadRenderTexture(ctx->rt_glow);
    UnloadRenderTexture(ctx->rt_depth);
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
    for (int i = 0; i < NG_RC_CASCADES_MAX; i++) {
      UnloadRenderTexture(ctx->rt_ws_casc[i]);
    }
    UnloadRenderTexture(ctx->rt_ws_stamp);
    UnloadRenderTexture(ctx->rt_ws_sh);
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
  if (!ng_rc_ws_ensure(&ctx->ws_cpu, ctx->rc_quality)) {
    return false;
  }
  ctx->ws_cpu.origin[0] = ctx->ws_origin[0];
  ctx->ws_cpu.origin[1] = ctx->ws_origin[1];
  ctx->ws_cpu.origin[2] = ctx->ws_origin[2];
  ctx->ws_cpu.size[0] = ctx->ws_size[0];
  ctx->ws_cpu.size[1] = ctx->ws_size[1];
  ctx->ws_cpu.size[2] = ctx->ws_size[2];

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
  if (ctx->ws_rt_ready && !ws_ok) {
    for (int i = 0; i < NG_RC_CASCADES_MAX; i++) {
      UnloadRenderTexture(ctx->rt_ws_casc[i]);
    }
    UnloadRenderTexture(ctx->rt_ws_stamp);
    UnloadRenderTexture(ctx->rt_ws_sh);
    UnloadRenderTexture(ctx->rt_ws[0]);
    UnloadRenderTexture(ctx->rt_ws[1]);
    ctx->ws_rt_ready = false;
  }
  if (!ctx->ws_rt_ready) {
    const int aw = probe_n * ws_dirs;
    const int ah = probe_n * probe_n;
    const int sh_w = probe_n * 4;
    for (int i = 0; i < NG_RC_CASCADES_MAX; i++) {
      ctx->rt_ws_casc[i] = LoadRenderTexture(aw, ah);
      SetTextureFilter(ctx->rt_ws_casc[i].texture, TEXTURE_FILTER_POINT);
      BeginTextureMode(ctx->rt_ws_casc[i]);
      ClearBackground(BLACK);
      EndTextureMode();
    }
    ctx->rt_ws_stamp = LoadRenderTexture(aw, ah);
    SetTextureFilter(ctx->rt_ws_stamp.texture, TEXTURE_FILTER_POINT);
    BeginTextureMode(ctx->rt_ws_stamp);
    ClearBackground(BLACK);
    EndTextureMode();
    ctx->rt_ws_sh = LoadRenderTexture(sh_w, ah);
    SetTextureFilter(ctx->rt_ws_sh.texture, TEXTURE_FILTER_POINT);
    BeginTextureMode(ctx->rt_ws_sh);
    ClearBackground(BLACK);
    EndTextureMode();
    ctx->rt_ws[0] = LoadRenderTexture(w, h);
    ctx->rt_ws[1] = LoadRenderTexture(w, h);
    SetTextureFilter(ctx->rt_ws[0].texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(ctx->rt_ws[1].texture, TEXTURE_FILTER_BILINEAR);
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
    SetShaderValue(pass->sh.handle, pass->loc_ws_origin, ctx->ws_origin, SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_ws_size >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_ws_size, ctx->ws_size, SHADER_UNIFORM_VEC3);
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
  const float gi = ctx->gi_strength;
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
    SetShaderValue(pass->sh.handle, pass->loc_ws_origin, ctx->ws_origin, SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_ws_size >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_ws_size, ctx->ws_size, SHADER_UNIFORM_VEC3);
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

/** Fill one WS cascade interval into rt_ws_casc[c] (SDF prim march). */
static void mod_render_rc_ws_casc_fill(ModRenderCtx *ctx, int c, float t0, float t1) {
  // agent: composer-2.5 | 2026-08-10 | tick bind rebuild_prims | c31da5
  NgRcPassShader *pass = &ctx->rc_ws_fill;
  const float sky[3] = {NG_RC_SKY.x, NG_RC_SKY.y, NG_RC_SKY.z};
  const float probe_res = (float)(ctx->ws_probe_n > 0 ? ctx->ws_probe_n : 12);
  const int dirs = ctx->ws_dirs > 0 ? ctx->ws_dirs : 8;
  const int steps = ctx->ws_cpu.steps > 0 ? ctx->ws_cpu.steps : 5;
  const int prim_count = ctx->ws_cpu.prim_count;
  RenderTexture2D *dest = &ctx->rt_ws_casc[c];
  BeginTextureMode(*dest);
  ClearBackground(BLACK);
  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  if (pass->loc_ws_origin >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_ws_origin, ctx->ws_origin, SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_ws_size >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_ws_size, ctx->ws_size, SHADER_UNIFORM_VEC3);
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
  mod_render_ws_fs_draw(ctx->ws_cpu.tex_prim, dest->texture.width, dest->texture.height);
  EndShaderMode();
  EndTextureMode();
}

/** T-merge near + far → dest RT (same atlas texel). */
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

/** Encode merged dir atlas → L1 SH (N*4 × N*N). */
static void mod_render_rc_ws_sh_encode(ModRenderCtx *ctx, Texture2D merged) {
  NgRcPassShader *pass = &ctx->rc_ws_sh_encode;
  const float probe_res = (float)(ctx->ws_probe_n > 0 ? ctx->ws_probe_n : 12);
  const int dirs = ctx->ws_dirs > 0 ? ctx->ws_dirs : 8;
  const float cres[2] = {(float)merged.width, (float)merged.height};
  BeginTextureMode(ctx->rt_ws_sh);
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
  DrawRectangle(0, 0, ctx->rt_ws_sh.texture.width, ctx->rt_ws_sh.texture.height, WHITE);
  EndShaderMode();
  EndTextureMode();
}

/** Soft-nearest SH eval → screen irr. */
static void mod_render_rc_ws_resolve(ModRenderCtx *ctx, int dest) {
  NgRcPassShader *pass = &ctx->rc_ws_resolve;
  const float sky[3] = {NG_RC_SKY.x, NG_RC_SKY.y, NG_RC_SKY.z};
  const float probe_res = (float)(ctx->ws_probe_n > 0 ? ctx->ws_probe_n : 12);
  const float sres[2] = {(float)ctx->rt_ws_sh.texture.width, (float)ctx->rt_ws_sh.texture.height};
  const float res[2] = {(float)ctx->rt_ws[dest].texture.width, (float)ctx->rt_ws[dest].texture.height};
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
  if (pass->loc_ws_origin >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_ws_origin, ctx->ws_origin, SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_ws_size >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_ws_size, ctx->ws_size, SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_probe_res >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_probe_res, &probe_res, SHADER_UNIFORM_FLOAT);
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
      SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_ws_sh.texture);
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
  const float sky[3] = {NG_RC_SKY.x, NG_RC_SKY.y, NG_RC_SKY.z};
  const float probe_res = (float)(ctx->ws_probe_n > 0 ? ctx->ws_probe_n : 12);
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
  if (pass->loc_sky >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_sky, sky, SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_ws_origin >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_ws_origin, ctx->ws_origin, SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_ws_size >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_ws_size, ctx->ws_size, SHADER_UNIFORM_VEC3);
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


/** Fixed cube locked to look-at; orbit must not slide the probe lattice. */
static void mod_render_rc_ws_update_frustum_aabb(ModRenderCtx *ctx) {
  // agent: composer-2.5 | 2026-08-10 | look-at lock clip no cam follow | 66d9b3
  const Camera3D *cam = &ctx->camera;
  const float cell = NG_RC_WS_CELL;
  const float extent = cell * (float)NG_RC_WS_VOX_RES;
  const Vector3 size = {extent, extent, extent};

  /* rc.js orbits with a fixed target — cam-biased / frustum-AABB placement slides the
   * brick every frame and remaps size/N or UVW → cell crawl + blink. Anchor on target. */
  const Vector3 anchor = cam->target;
  Vector3 desired = {anchor.x - extent * 0.5f, anchor.y - extent * 0.5f,
                     anchor.z - extent * 0.5f};
  desired.x = floorf(desired.x / cell) * cell;
  desired.y = floorf(desired.y / cell) * cell;
  desired.z = floorf(desired.z / cell) * cell;

  Vector3 origin = {ctx->ws_origin[0], ctx->ws_origin[1], ctx->ws_origin[2]};
  const float hold = extent * 0.25f; /* pan moves brick; pure orbit (fixed target) never does */
  const int need_init = (ctx->ws_size[0] < extent * 0.5f);
  if (need_init || fabsf(desired.x - origin.x) >= hold || fabsf(desired.y - origin.y) >= hold ||
      fabsf(desired.z - origin.z) >= hold) {
    origin = desired;
  }

  ctx->ws_origin[0] = origin.x;
  ctx->ws_origin[1] = origin.y;
  ctx->ws_origin[2] = origin.z;
  ctx->ws_size[0] = size.x;
  ctx->ws_size[1] = size.y;
  ctx->ws_size[2] = size.z;
  ctx->ws_cpu.origin[0] = origin.x;
  ctx->ws_cpu.origin[1] = origin.y;
  ctx->ws_cpu.origin[2] = origin.z;
  ctx->ws_cpu.size[0] = size.x;
  ctx->ws_cpu.size[1] = size.y;
  ctx->ws_cpu.size[2] = size.z;
}

/** True if frustum snap or scene content changed enough to restamp vox. */
static bool mod_render_rc_ws_vox_dirty(ModRenderCtx *ctx) {
  const uint32_t h = ng_rc_ws_scene_hash();
  const float eps = 1e-3f;
  const int moved =
      fabsf(ctx->ws_origin[0] - ctx->ws_snap_origin[0]) > eps ||
      fabsf(ctx->ws_origin[1] - ctx->ws_snap_origin[1]) > eps ||
      fabsf(ctx->ws_origin[2] - ctx->ws_snap_origin[2]) > eps ||
      fabsf(ctx->ws_size[0] - ctx->ws_snap_size[0]) > eps ||
      fabsf(ctx->ws_size[1] - ctx->ws_snap_size[1]) > eps ||
      fabsf(ctx->ws_size[2] - ctx->ws_snap_size[2]) > eps;
  if (!moved && h == ctx->ws_vox_scene_hash) {
    return false;
  }
  ctx->ws_snap_origin[0] = ctx->ws_origin[0];
  ctx->ws_snap_origin[1] = ctx->ws_origin[1];
  ctx->ws_snap_origin[2] = ctx->ws_origin[2];
  ctx->ws_snap_size[0] = ctx->ws_size[0];
  ctx->ws_snap_size[1] = ctx->ws_size[1];
  ctx->ws_snap_size[2] = ctx->ws_size[2];
  ctx->ws_vox_scene_hash = h;
  return true;
}

/** rc-ws steps 3–4: dirty prims → SDF fill → T-merge → SH resolve. */
static void mod_render_rc_gpu_tick(ModRenderCtx *ctx) {
  // agent: composer-2.5 | 2026-08-10 | tick skip rebuild_vox | bb5602
  if (mod_render_rc_ws_vox_dirty(ctx)) {
    ng_rc_ws_rebuild_prims(&ctx->ws_cpu);
  }

  const int probe_n = ctx->ws_probe_n > 0 ? ctx->ws_probe_n : 12;
  int nc = ctx->ws_cpu.cascades > 0 ? ctx->ws_cpu.cascades : 3;
  if (nc > NG_RC_CASCADES_MAX) {
    nc = NG_RC_CASCADES_MAX;
  }
  float cell = ctx->ws_size[0];
  if (ctx->ws_size[1] < cell) {
    cell = ctx->ws_size[1];
  }
  if (ctx->ws_size[2] < cell) {
    cell = ctx->ws_size[2];
  }
  cell /= (float)probe_n;
  // agent: composer-2.5 | 2026-08-10 | tick cascade interval tune | 7c0ad2
  float t0 = cell * 0.55f;
  float t1 = cell * 1.45f;
  for (int c = 0; c < nc; c++) {
    mod_render_rc_ws_casc_fill(ctx, c, t0, t1);
    t0 = t1;
    t1 *= 2.0f;
  }

  Texture2D merged = ctx->rt_ws_casc[nc - 1].texture;
  int to_stamp = 1;
  for (int c = nc - 2; c >= 0; c--) {
    RenderTexture2D *dest = to_stamp ? &ctx->rt_ws_stamp : &ctx->rt_ws_casc[nc - 1];
    mod_render_rc_ws_merge_to(ctx, ctx->rt_ws_casc[c].texture, merged, dest);
    merged = dest->texture;
    to_stamp ^= 1;
  }

  mod_render_rc_ws_sh_encode(ctx, merged);
  mod_render_rc_ws_resolve(ctx, ctx->ws_ping & 1);

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
}

static bool mod_render_ensure_gbuf(ModRenderCtx *ctx) {
  int w = 0;
  int h = 0;
  mod_render_internal_size(ctx, &w, &h);
  if (w <= 0 || h <= 0) {
    return false;
  }
  if (!ctx->gbuf_shader_ready) {
    ctx->gbuf_shader = ng_shader_load(NG_RES_ROOT "shaders/mesh.vs", NG_RES_ROOT "shaders/rc_gbuf.fs");
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
  ctx->rt_depth = LoadRenderTexture(w, h);
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
  mod_scene_graph_expire_live_draw(GetTime());
  const int n = mod_scene_graph_inst_count();
  for (int i = 0; i < n; i++) {
    const NgSceneInst *inst = mod_scene_graph_inst_at(i);
    if (!inst || !inst->model[0]) {
      continue;
    }
    if (!mod_render_asset_for_model(ctx, inst->model)) {
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
    (void)mod_render_batch_push(b, mod_render_pose_matrix(pos[0], pos[1], pos[2], rot, inst->scale));
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
  ClearBackground(BLACK);
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
      /* Frustum volume must match gbuf UVW packing this frame. */
      // agent: composer-2.5 | 2026-08-10 | frustum before gbuf CPU stamp | fff863
      mod_render_rc_ws_update_frustum_aabb(ctx);
      mod_render_collect_graph_batches(ctx);
      mod_render_fill_gbuf_graph(ctx, &ctx->rt_albedo, 0);
      mod_render_fill_gbuf_graph(ctx, &ctx->rt_normal, 1);
      mod_render_fill_gbuf_graph(ctx, &ctx->rt_glow, 2);
      mod_render_fill_gbuf_graph(ctx, &ctx->rt_depth, 3);

      bool rc_ready = false;
      if (want_rc && mod_render_ensure_rc(ctx)) {
        mod_render_rc_gpu_tick(ctx);
        rc_ready = ctx->rc_rt_ready && ctx->ws_rt_ready && ctx->rc_compose.ready &&
                   ctx->ws_cpu.prim_tex_ready;
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
  ctx->ws_origin[0] = -6.0f;
  ctx->ws_origin[1] = -0.5f;
  ctx->ws_origin[2] = -6.0f;
  ctx->ws_size[0] = 12.0f;
  ctx->ws_size[1] = 5.5f;
  ctx->ws_size[2] = 12.0f;
  ng_rc_ws_init(&ctx->ws_cpu);
  ctx->ws_cpu.origin[0] = ctx->ws_origin[0];
  ctx->ws_cpu.origin[1] = ctx->ws_origin[1];
  ctx->ws_cpu.origin[2] = ctx->ws_origin[2];
  ctx->ws_cpu.size[0] = ctx->ws_size[0];
  ctx->ws_cpu.size[1] = ctx->ws_size[1];
  ctx->ws_cpu.size[2] = ctx->ws_size[2];
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
