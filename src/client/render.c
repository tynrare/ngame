// agent: composer-2.5 | 2026-07-25 | client render module | g0j28e
// agent: composer-2.5 | 2026-07-28 | render drop embedded path | f42f1c
// agent: composer-2.5 | 2026-08-09 | shader glow rough metal uniforms | 7e0b28
// agent: composer-2.5 | 2026-08-09 | gbuffer RTs debug blit | 96d6a0
// agent: composer-2.5 | 2026-08-09 | instanced draw batch pools | 8837bc
// agent: composer-2.5 | 2026-08-09 | Phase2 SS RC render path | ff1b7f
// agent: composer-2.5 | 2026-08-09 | gate RC by scene render mode | 2a6d5a
#include "render.h"
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NG_RC_CASCADES 3

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
  int loc_tex_self;
  int loc_tex_parent;
  int loc_cascade;
  int loc_sky;
  int loc_merge_weight;
  int loc_gi_strength;
  bool ready;
} NgRcPassShader;

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
  RenderTexture2D rt_albedo;
  RenderTexture2D rt_normal;
  RenderTexture2D rt_glow;
  RenderTexture2D rt_depth;
  bool gbuf_ready;
  int gbuf_w;
  int gbuf_h;
  NgShader gbuf_shader;
  bool gbuf_shader_ready;
  RenderTexture2D rt_cascade[NG_RC_CASCADES];
  RenderTexture2D rt_merge;
  bool rc_rt_ready;
  int rc_w;
  int rc_h;
  NgRcPassShader rc_fill;
  NgRcPassShader rc_merge;
  NgRcPassShader rc_compose;
} ModRenderCtx;

static ModRenderCtx g_render_ctx;
static const Vector3 NG_RC_SKY = {0.08f, 0.10f, 0.14f};

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
    for (int i = 0; i < NG_RC_CASCADES; i++) {
      UnloadRenderTexture(ctx->rt_cascade[i]);
    }
    UnloadRenderTexture(ctx->rt_merge);
    ctx->rc_rt_ready = false;
    ctx->rc_w = 0;
    ctx->rc_h = 0;
  }
  if (ctx->rc_fill.ready) {
    ng_shader_unload(&ctx->rc_fill.sh);
    ctx->rc_fill.ready = false;
  }
  if (ctx->rc_merge.ready) {
    ng_shader_unload(&ctx->rc_merge.sh);
    ctx->rc_merge.ready = false;
  }
  if (ctx->rc_compose.ready) {
    ng_shader_unload(&ctx->rc_compose.sh);
    ctx->rc_compose.ready = false;
  }
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
  pass->loc_tex_self = GetShaderLocation(pass->sh.handle, "tex_self");
  pass->loc_tex_parent = GetShaderLocation(pass->sh.handle, "tex_parent");
  pass->loc_cascade = GetShaderLocation(pass->sh.handle, "ng_cascade");
  pass->loc_sky = GetShaderLocation(pass->sh.handle, "ng_sky");
  pass->loc_merge_weight = GetShaderLocation(pass->sh.handle, "ng_merge_weight");
  pass->loc_gi_strength = GetShaderLocation(pass->sh.handle, "ng_gi_strength");
  pass->ready = true;
  return true;
}

static bool mod_render_ensure_rc(ModRenderCtx *ctx) {
  const int w = ng_viewport_width();
  const int h = ng_viewport_height();
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
  if (!ctx->rc_compose.ready &&
      !mod_render_load_rc_pass(&ctx->rc_compose, NG_RES_ROOT "shaders/rc_compose.fs")) {
    return false;
  }
  if (ctx->rc_rt_ready && ctx->rc_w == w && ctx->rc_h == h) {
    return true;
  }
  if (ctx->rc_rt_ready) {
    for (int i = 0; i < NG_RC_CASCADES; i++) {
      UnloadRenderTexture(ctx->rt_cascade[i]);
    }
    UnloadRenderTexture(ctx->rt_merge);
    ctx->rc_rt_ready = false;
  }
  for (int i = 0; i < NG_RC_CASCADES; i++) {
    const int cw = w >> i;
    const int ch = h >> i;
    ctx->rt_cascade[i] = LoadRenderTexture(cw > 0 ? cw : 1, ch > 0 ? ch : 1);
  }
  ctx->rt_merge = LoadRenderTexture(w, h);
  ctx->rc_w = w;
  ctx->rc_h = h;
  ctx->rc_rt_ready = true;
  return true;
}

static void mod_render_fs_draw(Texture2D carrier, int dest_w, int dest_h) {
  const Rectangle src = {0.0f, 0.0f, (float)carrier.width, -(float)carrier.height};
  const Rectangle dst = {0.0f, 0.0f, (float)dest_w, (float)dest_h};
  DrawTexturePro(carrier, src, dst, (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
}

static void mod_render_rc_fill_cascade(ModRenderCtx *ctx, int c) {
  NgRcPassShader *pass = &ctx->rc_fill;
  RenderTexture2D *dest = &ctx->rt_cascade[c];
  const float res[2] = {(float)ctx->gbuf_w, (float)ctx->gbuf_h};
  const float sky[3] = {NG_RC_SKY.x, NG_RC_SKY.y, NG_RC_SKY.z};
  BeginTextureMode(*dest);
  ClearBackground(BLACK);
  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  if (pass->sh.loc_resolution >= 0) {
    SetShaderValue(pass->sh.handle, pass->sh.loc_resolution, res, SHADER_UNIFORM_VEC2);
  }
  if (pass->loc_cascade >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_cascade, &c, SHADER_UNIFORM_INT);
  }
  if (pass->loc_sky >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_sky, sky, SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_tex_depth >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_depth, ctx->rt_depth.texture);
  }
  if (pass->loc_tex_albedo >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_albedo, ctx->rt_albedo.texture);
  }
  if (pass->loc_tex_glow >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_glow, ctx->rt_glow.texture);
  }
  mod_render_fs_draw(ctx->rt_depth.texture, dest->texture.width, dest->texture.height);
  EndShaderMode();
  EndTextureMode();
}

static void mod_render_rc_merge(ModRenderCtx *ctx, int child, int parent) {
  NgRcPassShader *pass = &ctx->rc_merge;
  const float weight = 1.0f;
  BeginTextureMode(ctx->rt_merge);
  ClearBackground(BLACK);
  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  if (pass->loc_merge_weight >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_merge_weight, &weight, SHADER_UNIFORM_FLOAT);
  }
  if (pass->loc_tex_self >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_self, ctx->rt_cascade[child].texture);
  }
  if (pass->loc_tex_parent >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_parent, ctx->rt_cascade[parent].texture);
  }
  if (pass->loc_tex_depth >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_depth, ctx->rt_depth.texture);
  }
  mod_render_fs_draw(ctx->rt_cascade[child].texture, ctx->rt_merge.texture.width,
                     ctx->rt_merge.texture.height);
  EndShaderMode();
  EndTextureMode();

  /* Copy merge result into child cascade (full-res merge buffer → child size via blit). */
  BeginTextureMode(ctx->rt_cascade[child]);
  ClearBackground(BLACK);
  mod_render_fs_draw(ctx->rt_merge.texture, ctx->rt_cascade[child].texture.width,
                     ctx->rt_cascade[child].texture.height);
  EndTextureMode();
}

static void mod_render_rc_compose(ModRenderCtx *ctx) {
  NgRcPassShader *pass = &ctx->rc_compose;
  const float gi = 0.85f;
  const float sky[3] = {NG_RC_SKY.x, NG_RC_SKY.y, NG_RC_SKY.z};
  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  if (pass->loc_gi_strength >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_gi_strength, &gi, SHADER_UNIFORM_FLOAT);
  }
  if (pass->loc_sky >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_sky, sky, SHADER_UNIFORM_VEC3);
  }
  if (pass->loc_tex_albedo >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_albedo, ctx->rt_albedo.texture);
  }
  if (pass->loc_tex_normal >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_normal, ctx->rt_normal.texture);
  }
  if (pass->loc_tex_glow >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_glow, ctx->rt_glow.texture);
  }
  if (pass->loc_tex_depth >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_depth, ctx->rt_depth.texture);
  }
  if (pass->loc_tex_irradiance >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_irradiance, ctx->rt_cascade[0].texture);
  }
  mod_render_blit_rt(&ctx->rt_albedo);
  EndShaderMode();
}

static bool mod_render_ensure_gbuf(ModRenderCtx *ctx) {
  const int w = ng_viewport_width();
  const int h = ng_viewport_height();
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
  BeginTextureMode(*rt);
  ClearBackground(BLACK);
  BeginMode3D(ctx->camera);
  mod_render_flush_batches_gbuf(ctx, mode);
  EndMode3D();
  EndTextureMode();
}

static void mod_render_blit_rt(const RenderTexture2D *rt) {
  const Rectangle src = {0.0f, 0.0f, (float)rt->texture.width, -(float)rt->texture.height};
  const Rectangle dst = {0.0f, 0.0f, (float)GetScreenWidth(), (float)GetScreenHeight()};
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
  ClearBackground(mod_render_bg_color());
  mod_render_update_camera(ctx);

  mod_scene_runtime_use_view();
  const bool graph = mod_scene_view_graph_active() ||
                     (mod_scene_view_is_loaded() && mod_scene_graph_inst_count() > 0) ||
                     (mod_net_is_authoritative() && mod_scene_is_loaded());

  // agent: composer-2.5 | 2026-08-09 | gate RC by scene render mode | 2a6d5a
  const NgSceneViewMeta *vmeta = mod_scene_assets_view();
  const NgSceneRenderMode rmode =
      (vmeta && vmeta->valid) ? vmeta->render_mode : NG_SCENE_RENDER_SIMPLE;
  const bool feature_gbuf =
      rmode == NG_SCENE_RENDER_GBUFFER || rmode == NG_SCENE_RENDER_RC;
  const bool feature_rc = rmode == NG_SCENE_RENDER_RC;
  const bool want_rc = graph && feature_rc && ctx->rc_quality >= 2;
  const bool want_gbuf =
      graph && feature_gbuf && (want_rc || ctx->debug_pass != NG_RENDER_PASS_FINAL);

  if (want_gbuf) {
    if (mod_net_is_authoritative() && !mod_scene_view_graph_active() && mod_scene_is_loaded()) {
      mod_scene_runtime_use_server();
    } else {
      mod_scene_runtime_use_view();
    }
    if (mod_render_ensure_gbuf(ctx)) {
      mod_render_collect_graph_batches(ctx);
      mod_render_fill_gbuf_graph(ctx, &ctx->rt_albedo, 0);
      mod_render_fill_gbuf_graph(ctx, &ctx->rt_normal, 1);
      mod_render_fill_gbuf_graph(ctx, &ctx->rt_glow, 2);
      mod_render_fill_gbuf_graph(ctx, &ctx->rt_depth, 3);

      if (want_rc && mod_render_ensure_rc(ctx)) {
        for (int c = NG_RC_CASCADES - 1; c >= 0; c--) {
          mod_render_rc_fill_cascade(ctx, c);
        }
        /* Merge coarse → fine (nearest). */
        for (int c = NG_RC_CASCADES - 2; c >= 0; c--) {
          mod_render_rc_merge(ctx, c, c + 1);
        }
      }

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
      } else if (ctx->debug_pass == NG_RENDER_PASS_IRRADIANCE && ctx->rc_rt_ready) {
        ClearBackground(BLACK);
        mod_render_blit_rt(&ctx->rt_cascade[0]);
      } else if (want_rc && ctx->rc_rt_ready) {
        ClearBackground(mod_render_bg_color());
        mod_render_rc_compose(ctx);
      } else if (ctx->debug_pass != NG_RENDER_PASS_FINAL) {
        ClearBackground(BLACK);
        mod_render_blit_rt(&ctx->rt_albedo);
      } else {
        /* quality < 2 and final: fall through to forward */
        goto forward_lit;
      }
      mod_render_draw_overlay(mod_render_authoritative_label(ctx), 10);
      if (ctx->debug_pass != NG_RENDER_PASS_FINAL) {
        DrawText(TextFormat("pass=%s q=%d", mod_render_pass_name(ctx->debug_pass), ctx->rc_quality),
                 10, 34, 18, LIME);
      } else if (want_rc) {
        DrawText(TextFormat("rc_quality=%d", ctx->rc_quality), 10, 34, 18, LIME);
      }
      return;
    }
  }

forward_lit:
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

  mod_render_draw_overlay(mod_render_authoritative_label(ctx), 10);
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
  ctx->rc_quality = 2;
  mod_render_init_camera(ctx);
  return true;
}

static void mod_render_shutdown(void *vctx) {
  ModRenderCtx *ctx = (ModRenderCtx *)vctx;
  mod_render_clear_cache(ctx);
  mod_render_unload_gbuf(ctx);
  mod_render_unload_rc(ctx);
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
  if (strcmp(path, "debug.render.pass") == 0) {
    NgRenderDebugPass pass;
    if (!mod_render_pass_from_name(value, &pass)) {
      return false;
    }
    g_render_ctx.debug_pass = pass;
    return true;
  }
  if (strcmp(path, "debug.render.rc_quality") == 0) {
    const int q = atoi(value);
    if (q < 0 || q > 2) {
      return false;
    }
    g_render_ctx.rc_quality = q;
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
  if (strcmp(path, "debug.render.rc_quality") == 0) {
    snprintf(out, cap, "%d", g_render_ctx.rc_quality);
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
