// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
/* Shared rendering scope index: frame-draw (scene/draw.c) owns command lifetimes;
 * material-recipe (scene/assets.c) owns handles/resources; font-msdf (font_msdf.c)
 * owns glyph layout; shape-submit (shape.c) owns GPU submission.
 * Ordering: frame-draw steps 1–2, material-recipe step 2, scoped opaque batches,
 * adjacent transparent shape runs, then font-msdf steps 6–8 after composition.
 * Scope/pass are implicit in each collection; retained slots do not cross passes.
 * Shared invariant: transient commands and labels never enter the GI registry.
 */
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
// agent: grok-4.6 | 2026-08-21 | strip dead clip SS probe paths | 36a296
#include "render.h"
#include "render_rc_ws.h"
#include "font_msdf.h"
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
#include "shape.h"
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
  // agent: grok-4.6 | 2026-08-31 | load bind albedo texture | 85e565
  Texture2D albedo;
  bool have_albedo;
} RenderAsset;

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
// agent: gpt-6-astra | 2026-09-05 | size draw caches for shared queue | 2614ef
#define NG_RENDER_CACHE_MAX (NG_SCENE_ASSET_MAX + 2)
#define NG_RENDER_BATCH_MAX NG_DRAW_MAX

typedef struct NgInstanceBatch {
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
// agent: gpt-6-astra | 2026-09-05 | batch shape material tint | eeba45
  char model[32];
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  uint8_t tint[3];
  NgMaterialHandle material;
  bool transparent;
  NgShapeInstance *mats;
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
  int loc_tex_irradiance_ws;
  int loc_tex_self;
  int loc_tex_parent;
  int loc_sky;
  int loc_gi_strength;
  int loc_dir_count;
  int loc_dir_side;
  int loc_interval0;
  int loc_interval1;
  int loc_max_steps;
  int loc_cascade_res;
  int loc_cam_pos;
  int loc_ws_origin;
  int loc_ws_size;
  int loc_probe_res;
  bool ready;
} NgRcPassShader;

#define NG_RC_CASCADES_MAX 3

typedef struct ModRenderCtx {
  RenderAssetCacheEntry cache[NG_RENDER_CACHE_MAX];
  int cache_count;
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  RenderAsset material_cache[NG_SCENE_ASSET_MAX*2];
  uint32_t material_ids[NG_SCENE_ASSET_MAX*2];
  int material_count;
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
  NgShader blit_rgb;
  bool blit_rgb_ready;
  RenderTexture2D rt_ws[2];
  int ws_ping;
  bool ws_rt_ready;
  int ws_irr_w;
  int ws_irr_h;
  uint32_t ws_vox_scene_hash;
  NgRcWsCtx ws_cpu;
  NgRcPassShader rc_compose;
  NgRcPassShader rc_ws_chunk_fill;
  NgRcPassShader rc_ws_chunk_merge;
  NgRcPassShader rc_ws_chunk_encode;
  NgRcPassShader rc_ws_chunk_store;
  NgRcPassShader rc_ws_chunk_resolve;
  RenderTexture2D rt_chunk_casc[NG_RC_CASCADES_MAX];
  RenderTexture2D rt_chunk_merge;
  RenderTexture2D rt_chunk_merge_c1;
  RenderTexture2D rt_page_irr;
  RenderTexture2D rt_page_c0;
  RenderTexture2D rt_page_c1;
  RenderTexture2D rt_page_c2;
  bool chunk_gi_ready;
  uint32_t chunk_gi_scene_hash;
  NgRcPassShader rc_ws_debug;
  RenderTexture2D rt_prim_vis;
  bool ws_cull_ready;
  Vector4 ws_frustum[6];
  bool ws_frustum_valid;
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

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
typedef struct NgTextureCache { char path[160]; Texture2D texture; int refs; } NgTextureCache;
static NgTextureCache textures[NG_SCENE_ASSET_MAX*3];
/** @param path const char* texture path. @return Texture2D shared upload, zero on failure. */
static Texture2D render_texture_load(const char *path) {
  int slot=-1;
  for(int i=0;i<NG_SCENE_ASSET_MAX*3;i++) {
    if(textures[i].refs && !strcmp(textures[i].path,path)) { textures[i].refs++; return textures[i].texture; }
    if(!textures[i].refs && slot<0) slot=i;
  }
  if(slot<0) return (Texture2D){0};
  Image image=LoadImage(path); if(!image.data) return (Texture2D){0};
  ImageFlipVertical(&image); Texture2D t=LoadTextureFromImage(image); UnloadImage(image);
  if(t.id) { textures[slot].texture=t; textures[slot].refs=1; snprintf(textures[slot].path,160,"%s",path); }
  return t;
}
/** @param t Texture2D reference. @return void; last owner releases upload. */
static void render_texture_unload(Texture2D t) {
  for(int i=0;i<NG_SCENE_ASSET_MAX*3;i++) if(textures[i].refs && textures[i].texture.id==t.id) {
    if(!--textures[i].refs) UnloadTexture(t); return;
  }
}
/** @param a RenderAsset* output. @param resolved const NgSceneResolvedModel* recipe. @param fs_path const char* fragment. @param vs_path const char* vertex. @param geometry bool mesh ownership. @return void. */
static void mod_render_load_asset_mesh(RenderAsset *a, const NgSceneResolvedModel *resolved,
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
                                       const char *fs_path, const char *vs_path, bool geometry) {
  if (a->ready) {
    return;
  }
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  Mesh mesh={0};
  if (!geometry) { /* Material cache owns no geometry. */
  } else if (resolved->mesh_kind == NG_SCENE_MESH_SPHERE) {
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
  a->have_albedo = false;
  a->albedo = (Texture2D){0};
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  if(geometry) a->model=LoadModelFromMesh(mesh);
  else {
    a->model.materialCount=1; a->model.materials=MemAlloc(sizeof(Material));
    a->model.materials[0]=LoadMaterialDefault();
  }
  a->shader = ng_shader_load(vs_path, fs_path);
  if (a->shader.handle.id == 0) {
    UnloadModel(a->model);
    return;
  }
  a->model.materials[0].shader = a->shader.handle;
  // agent: grok-4.6 | 2026-08-31 | load bind albedo texture | 85e565
  // agent: grok-4.6 | 2026-08-31 | flip albedo image vertical | 1576d2
  if (resolved->albedo[0]) {
    char tex_path[160];
    snprintf(tex_path, sizeof(tex_path), NG_RES_ROOT "%s",
             resolved->albedo[0] == '/' ? resolved->albedo + 1 : resolved->albedo);
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
    a->albedo=render_texture_load(tex_path);
    if (a->albedo.id != 0) {
      SetTextureFilter(a->albedo, TEXTURE_FILTER_POINT);
      SetMaterialTexture(&a->model.materials[0], MATERIAL_MAP_ALBEDO, a->albedo);
      a->have_albedo = true;
    }
  }
  a->ready = true;
}

static void mod_render_unload_asset(RenderAsset *a) {
  if (!a->ready) {
    return;
  }
  ng_shader_unload(&a->shader);
  if (a->have_albedo && a->albedo.id != 0) {
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
    render_texture_unload(a->albedo);
    a->albedo = (Texture2D){0};
    a->have_albedo = false;
  }
  UnloadModel(a->model);
  a->ready = false;
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
// agent: gpt-6-astra | 2026-09-05 | free retained batch scratch slots | 5cef5e
/** @param ctx ModRenderCtx* renderer. @return void; free all retained scope batch slots. */
static void mod_render_clear_batches(ModRenderCtx *ctx) {
  // agent: composer-2.5 | 2026-08-09 | instanced draw batch pools | 8837bc
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  for (int i = 0; i < NG_RENDER_BATCH_MAX; i++) {
    free(ctx->batches[i].mats);
    ctx->batches[i].mats = NULL;
    ctx->batches[i].count = 0;
    ctx->batches[i].capacity = 0;
    ctx->batches[i].model[0] = '\0';
  }
  ctx->batch_count = 0;
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
// agent: gpt-6-astra | 2026-09-05 | release scene font cache with meshes | 624d9e
/** @param ctx ModRenderCtx* renderer. @return void; release scene GPU resources. */
static void mod_render_clear_cache(ModRenderCtx *ctx) {
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  mod_font_msdf_shutdown();
  for(int i=0;i<ctx->material_count;i++) mod_render_unload_asset(&ctx->material_cache[i]);
  ctx->material_count=0;
  for (int i = 0; i < ctx->cache_count; i++) {
    mod_render_unload_asset(&ctx->cache[i].asset);
  }
  ctx->cache_count = 0;
  mod_render_clear_batches(ctx);
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
// agent: gpt-6-astra | 2026-09-05 | reuse batch slots across animated tints | 59630c
/** @param ctx ModRenderCtx* renderer. @return void; reassign keys each pass, retaining matrix storage. */
static void mod_render_batches_reset_counts(ModRenderCtx *ctx) {
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  ctx->batch_count = 0;
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
/** @param b NgInstanceBatch* storage. @param need int minimum capacity. @return bool storage available. */
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
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  NgShapeInstance *next = realloc(b->mats, (size_t)cap * sizeof(*next));
  if (!next) {
    return false;
  }
  b->mats = next;
  b->capacity = cap;
  return true;
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
// agent: gpt-6-astra | 2026-09-05 | key shape batches by tint | 544c59
/** @param ctx ModRenderCtx* renderer. @param key const char* mesh/material. @param tint const uint8_t[3] multiplier or NULL white. @param material NgMaterialHandle batch recipe. @return NgInstanceBatch* reusable batch or NULL. */
static NgInstanceBatch *mod_render_batch_get(ModRenderCtx *ctx, const char *key, const uint8_t *tint, NgMaterialHandle material) {
  const uint8_t white[3] = {255, 255, 255};
  if (!tint) tint = white;
  if (!ctx || !key || key[0] == '\0') {
    return NULL;
  }
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  const NgSceneMaterialDesc *recipe=ng_material_get(material);
  bool transparent=recipe && recipe->blend;
  const NgSceneModelDesc *geometry=mod_scene_assets_get_model(key);
  for (int i = transparent ? ctx->batch_count-1 : 0; i >= 0 && i < ctx->batch_count; i++) {
    const NgSceneModelDesc *other=mod_scene_assets_get_model(ctx->batches[i].model);
    bool same=!strcmp(ctx->batches[i].model,key) || (geometry && other && !strcmp(geometry->mesh,other->mesh));
    if (same && ctx->batches[i].material.id==material.id) {
      memcpy(ctx->batches[i].tint,tint,3);
      return &ctx->batches[i];
    }
  }
  if (ctx->batch_count >= NG_RENDER_BATCH_MAX) {
    return NULL;
  }
  NgInstanceBatch *b = &ctx->batches[ctx->batch_count++];
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  b->count = 0;
  strncpy(b->model, key, sizeof(b->model) - 1);
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  memcpy(b->tint, tint, 3);
  b->material=material; b->transparent=transparent;
  return b;
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
/** @param b NgInstanceBatch* storage. @param m Matrix pose. @return bool appended or allocation failed. */
static bool mod_render_batch_push(NgInstanceBatch *b, Matrix m) {
  if (!b) {
    return false;
  }
  if (!mod_render_batch_ensure(b, b->count + 1)) {
    return false;
  }
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  b->mats[b->count++] = ng_shape_instance(m,b->tint);
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
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  mod_render_load_asset_mesh(&entry->asset, resolved, fs_path, vs_path,true);
  return entry->asset.ready ? &entry->asset : NULL;
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
// agent: gpt-6-astra | 2026-09-05 | resolve shared unit physics meshes | 57e026
/** @param ctx ModRenderCtx* renderer. @param model_name const char* model/unit primitive key. @return RenderAsset* cached GPU resource or NULL. */
static RenderAsset *mod_render_asset_for_model(ModRenderCtx *ctx, const char *model_name) {
  mod_scene_runtime_use_view();
  if (!model_name || model_name[0] == '\0') {
    return NULL;
  }
  RenderAsset *cached = mod_render_cache_get(ctx, model_name);
  if (cached) {
    return cached;
  }
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
// agent: gpt-6-astra | 2026-09-05 | load unit geometry for physics draws | 845554
  NgSceneResolvedModel resolved;
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  if (!strcmp(model_name, "@draw_box") || !strcmp(model_name, "@draw_sphere")) {
    bool sphere = !strcmp(model_name, "@draw_sphere");
    /* Legacy model cubes apply 1.5 in the loader; cancel it for unit physics geometry. */
    float unit = sphere ? 1.0f : 1.0f / 1.5f;
    resolved = (NgSceneResolvedModel){.ok = true,
      .mesh_kind = sphere ? NG_SCENE_MESH_SPHERE : NG_SCENE_MESH_CUBE,
      .mesh_w = unit, .mesh_h = unit, .mesh_d = unit, .roughness = 1};
    strcpy(resolved.fragment, "shaders/flat.fs");
    strcpy(resolved.vertex, "shaders/mesh.vs");
    return mod_render_cache_put(ctx, model_name, &resolved);
  }
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
  if (ctx->blit_rgb_ready) {
    ng_shader_unload(&ctx->blit_rgb);
    ctx->blit_rgb_ready = false;
  }
}

// agent: grok-4.6 | 2026-08-21 | strip dead clip SS probe paths | 36a296
static void mod_render_unload_rc(ModRenderCtx *ctx) {
  if (ctx->ws_rt_ready) {
    UnloadRenderTexture(ctx->rt_ws[0]);
    UnloadRenderTexture(ctx->rt_ws[1]);
    ctx->ws_rt_ready = false;
  }
  if (ctx->chunk_gi_ready) {
    for (int i = 0; i < NG_RC_CASCADES_MAX; i++) {
      UnloadRenderTexture(ctx->rt_chunk_casc[i]);
    }
    UnloadRenderTexture(ctx->rt_chunk_merge);
    UnloadRenderTexture(ctx->rt_chunk_merge_c1);
    UnloadRenderTexture(ctx->rt_page_irr);
    UnloadRenderTexture(ctx->rt_page_c0);
    UnloadRenderTexture(ctx->rt_page_c1);
    UnloadRenderTexture(ctx->rt_page_c2);
    ctx->chunk_gi_ready = false;
  }
  ng_rc_ws_shutdown(&ctx->ws_cpu);
  if (ctx->rc_compose.ready) {
    ng_shader_unload(&ctx->rc_compose.sh);
    ctx->rc_compose.ready = false;
  }
  if (ctx->rc_ws_chunk_fill.ready) {
    ng_shader_unload(&ctx->rc_ws_chunk_fill.sh);
    ctx->rc_ws_chunk_fill.ready = false;
  }
  if (ctx->rc_ws_chunk_merge.ready) {
    ng_shader_unload(&ctx->rc_ws_chunk_merge.sh);
    ctx->rc_ws_chunk_merge.ready = false;
  }
  if (ctx->rc_ws_chunk_encode.ready) {
    ng_shader_unload(&ctx->rc_ws_chunk_encode.sh);
    ctx->rc_ws_chunk_encode.ready = false;
  }
  if (ctx->rc_ws_chunk_store.ready) {
    ng_shader_unload(&ctx->rc_ws_chunk_store.sh);
    ctx->rc_ws_chunk_store.ready = false;
  }
  if (ctx->rc_ws_chunk_resolve.ready) {
    ng_shader_unload(&ctx->rc_ws_chunk_resolve.sh);
    ctx->rc_ws_chunk_resolve.ready = false;
  }
  if (ctx->rc_ws_debug.ready) {
    ng_shader_unload(&ctx->rc_ws_debug.sh);
    ctx->rc_ws_debug.ready = false;
  }
  if (ctx->ws_cull_ready) {
    UnloadRenderTexture(ctx->rt_prim_vis);
    ctx->ws_cull_ready = false;
  }
  ctx->ws_frustum_valid = false;
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
  // agent: grok-4.6 | 2026-08-21 | opaque blit gbuf debug | 48f37a
  rlDisableColorBlend();
  DrawTexturePro(ctx->rt_present.texture, src, dst, (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
  rlEnableColorBlend();
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
  pass->loc_tex_irradiance_ws = GetShaderLocation(pass->sh.handle, "tex_irradiance_ws");
  pass->loc_tex_self = GetShaderLocation(pass->sh.handle, "tex_self");
  pass->loc_tex_parent = GetShaderLocation(pass->sh.handle, "tex_parent");
  pass->loc_sky = GetShaderLocation(pass->sh.handle, "ng_sky");
  pass->loc_gi_strength = GetShaderLocation(pass->sh.handle, "ng_gi_strength");
  pass->loc_dir_count = GetShaderLocation(pass->sh.handle, "ng_dir_count");
  pass->loc_dir_side = GetShaderLocation(pass->sh.handle, "ng_dir_side");
  pass->loc_interval0 = GetShaderLocation(pass->sh.handle, "ng_interval0");
  pass->loc_interval1 = GetShaderLocation(pass->sh.handle, "ng_interval1");
  pass->loc_max_steps = GetShaderLocation(pass->sh.handle, "ng_max_steps");
  pass->loc_cascade_res = GetShaderLocation(pass->sh.handle, "ng_cascade_res");
  pass->loc_cam_pos = GetShaderLocation(pass->sh.handle, "ng_cam_pos");
  pass->loc_ws_origin = GetShaderLocation(pass->sh.handle, "ng_ws_origin");
  pass->loc_ws_size = GetShaderLocation(pass->sh.handle, "ng_ws_size");
  pass->loc_probe_res = GetShaderLocation(pass->sh.handle, "ng_probe_res");
  pass->ready = true;
  return true;
}

// agent: grok-4.6 | 2026-08-21 | strip dead clip SS probe paths | 36a296
static bool mod_render_ensure_rc(ModRenderCtx *ctx) {
  int w = 0;
  int h = 0;
  mod_render_internal_size(ctx, &w, &h);
  if (w <= 0 || h <= 0) {
    return false;
  }
  if (!ctx->rc_compose.ready &&
      !mod_render_load_rc_pass(&ctx->rc_compose, NG_RES_ROOT "shaders/rc_compose.fs")) {
    return false;
  }
  if (!ctx->rc_ws_chunk_fill.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_chunk_fill, NG_RES_ROOT "shaders/rc_ws_chunk_fill.fs")) {
    return false;
  }
  if (!ctx->rc_ws_chunk_merge.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_chunk_merge, NG_RES_ROOT "shaders/rc_ws_chunk_merge.fs")) {
    return false;
  }
  if (!ctx->rc_ws_chunk_encode.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_chunk_encode,
                               NG_RES_ROOT "shaders/rc_ws_chunk_encode.fs")) {
    return false;
  }
  if (!ctx->rc_ws_chunk_store.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_chunk_store,
                               NG_RES_ROOT "shaders/rc_ws_chunk_store.fs")) {
    return false;
  }
  if (!ctx->rc_ws_chunk_resolve.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_chunk_resolve,
                               NG_RES_ROOT "shaders/rc_ws_chunk_resolve.fs")) {
    return false;
  }
  if (!ctx->rc_ws_debug.ready &&
      !mod_render_load_rc_pass(&ctx->rc_ws_debug, NG_RES_ROOT "shaders/rc_ws_debug.fs")) {
    return false;
  }
  if (!ng_rc_ws_ensure(&ctx->ws_cpu, ctx->rc_quality)) {
    return false;
  }
  if (!ctx->ws_cull_ready) {
    ctx->rt_prim_vis = LoadRenderTexture(NG_RC_WS_PRIM_MAX, 2);
    if (ctx->rt_prim_vis.id == 0) {
      return false;
    }
    SetTextureFilter(ctx->rt_prim_vis.texture, TEXTURE_FILTER_POINT);
    SetTextureWrap(ctx->rt_prim_vis.texture, TEXTURE_WRAP_CLAMP);
    ctx->ws_cull_ready = true;
  }
  const int ws_ok = ctx->ws_rt_ready && ctx->ws_irr_w == w && ctx->ws_irr_h == h;
  if (!ws_ok && ctx->ws_rt_ready) {
    UnloadRenderTexture(ctx->rt_ws[0]);
    UnloadRenderTexture(ctx->rt_ws[1]);
    ctx->ws_rt_ready = false;
  }
  if (!ctx->ws_rt_ready) {
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
    ctx->ws_irr_w = w;
    ctx->ws_irr_h = h;
    ctx->ws_ping = 0;
    ctx->ws_rt_ready = true;
    ctx->ws_vox_scene_hash = 0;
  }
  if (!ctx->chunk_gi_ready) {
    // agent: grok-4.6 | 2026-08-21 | atlas dirs 6 24 96 | d748ce
    static const int k_dirs[NG_RC_CASCADES_MAX] = {6, 24, 96};
    static const int k_cells[NG_RC_CASCADES_MAX] = {512, 64, 8};
    for (int c = 0; c < NG_RC_CASCADES_MAX; c++) {
      ctx->rt_chunk_casc[c] = mod_render_load_rt_rgba32f(k_dirs[c], k_cells[c]);
      if (ctx->rt_chunk_casc[c].id == 0) {
        return false;
      }
    }
    ctx->rt_chunk_merge = mod_render_load_rt_rgba32f(6, 512);
    ctx->rt_chunk_merge_c1 = mod_render_load_rt_rgba32f(24, 64);
    ctx->rt_page_irr = mod_render_load_rt_rgba32f(NG_RC_WS_PAGE_CAP * 8, 256);
    // agent: grok-4.6 | 2026-08-21 | persist merged C0 dir atlas | 7faa50
    ctx->rt_page_c0 = mod_render_load_rt_rgba32f(NG_RC_WS_PAGE_CAP * 6, 512);
    ctx->rt_page_c1 = mod_render_load_rt_rgba32f(NG_RC_WS_PAGE_CAP * 24, 64);
    ctx->rt_page_c2 = mod_render_load_rt_rgba32f(NG_RC_WS_PAGE_CAP * 96, 8);
    if (ctx->rt_chunk_merge.id == 0 || ctx->rt_chunk_merge_c1.id == 0 || ctx->rt_page_irr.id == 0 ||
        ctx->rt_page_c0.id == 0 || ctx->rt_page_c1.id == 0 || ctx->rt_page_c2.id == 0) {
      return false;
    }
    BeginTextureMode(ctx->rt_page_irr);
    ClearBackground(BLACK);
    EndTextureMode();
    BeginTextureMode(ctx->rt_page_c0);
    ClearBackground(BLACK);
    EndTextureMode();
    BeginTextureMode(ctx->rt_page_c1);
    ClearBackground(BLACK);
    EndTextureMode();
    BeginTextureMode(ctx->rt_page_c2);
    ClearBackground(BLACK);
    EndTextureMode();
    ctx->chunk_gi_ready = true;
    ctx->chunk_gi_scene_hash = 0;
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

// agent: grok-4.6 | 2026-08-21 | strip dead clip SS probe paths | 36a296
static void mod_render_rc_compose(ModRenderCtx *ctx) {
  NgRcPassShader *pass = &ctx->rc_compose;
  const float gi = ctx->gi_strength;
  const float sky[3] = {NG_RC_SKY.x, NG_RC_SKY.y, NG_RC_SKY.z};
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
  if (pass->loc_gi_strength >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_gi_strength, &gi, SHADER_UNIFORM_FLOAT);
  }
  if (pass->loc_sky >= 0) {
    SetShaderValue(pass->sh.handle, pass->loc_sky, sky, SHADER_UNIFORM_VEC3);
  }
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
  if (pass->loc_tex_glow >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_glow, ctx->rt_glow.texture);
  }
  if (pass->loc_tex_depth >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_depth, ctx->rt_depth.texture);
  }
  if (pass->loc_tex_irradiance_ws >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_irradiance_ws, ctx->rt_ws[ping].texture);
  }
  mod_render_fs_draw(ctx->rt_depth.texture, pw, ph);
  EndShaderMode();
}

// agent: grok-4.6 | 2026-08-21 | strip dead clip SS probe paths | 36a296
static void mod_render_rc_ws_debug(ModRenderCtx *ctx, int mode) {
  NgRcPassShader *pass = &ctx->rc_ws_debug;
  int pw = 0;
  int ph = 0;
  mod_render_internal_size(ctx, &pw, &ph);
  const float res[2] = {(float)pw, (float)ph};

  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  if (pass->sh.loc_resolution >= 0) {
    SetShaderValue(pass->sh.handle, pass->sh.loc_resolution, res, SHADER_UNIFORM_VEC2);
  }
  {
    const float eye[3] = {ctx->camera.position.x, ctx->camera.position.y, ctx->camera.position.z};
    int loc = GetShaderLocation(pass->sh.handle, "ng_eye");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, eye, SHADER_UNIFORM_VEC3);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "ng_debug_mode");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &mode, SHADER_UNIFORM_INT);
    }
  }
  {
    const float ext = ng_rc_ws_chunk_extent();
    int loc = GetShaderLocation(pass->sh.handle, "ng_chunk_extent");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &ext, SHADER_UNIFORM_FLOAT);
    }
  }
  {
    const int n = ctx->ws_cpu.chunk_vis_n;
    int locn = GetShaderLocation(pass->sh.handle, "ng_chunk_count");
    if (locn >= 0) {
      SetShaderValue(pass->sh.handle, locn, &n, SHADER_UNIFORM_INT);
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
    const float prim_count = (float)(ctx->ws_cpu.prim_count > 0 ? ctx->ws_cpu.prim_count : 0);
    int loc = GetShaderLocation(pass->sh.handle, "ng_prim_count");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &prim_count, SHADER_UNIFORM_FLOAT);
    }
  }
  rlDrawRenderBatchActive();
  if (pass->loc_tex_depth >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_depth, ctx->rt_depth.texture);
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_chunks");
    if (loc >= 0 && ctx->ws_cpu.chunk_vis_tex_ready) {
      SetShaderValueTexture(pass->sh.handle, loc, ctx->ws_cpu.tex_chunk_vis);
    } else if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_depth.texture);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_pages");
    if (loc >= 0 && ctx->ws_cpu.pages_tex_ready) {
      SetShaderValueTexture(pass->sh.handle, loc, ctx->ws_cpu.tex_pages);
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
    } else if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_depth.texture);
    }
  }
  mod_render_fs_draw(ctx->rt_depth.texture, pw, ph);
  EndShaderMode();
}

// agent: grok-4.6 | 2026-08-21 | strip dead clip SS probe paths | 36a296
static bool mod_render_rc_ws_vox_dirty(ModRenderCtx *ctx) {
  const uint32_t h = ng_rc_ws_scene_hash();
  if (h == ctx->ws_vox_scene_hash) {
    return false;
  }
  ctx->ws_vox_scene_hash = h;
  return true;
}

/** Inward frustum planes from camera basis (BeginMode3D look/fov). */
static void mod_render_frustum_planes(const Camera3D *cam, float aspect, Vector4 out[6]) {
  // agent: grok-4.6 | 2026-08-21 | restore camera-basis frustum planes | 75af91
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

  out[4].x = f.x;
  out[4].y = f.y;
  out[4].z = f.z;
  out[4].w = -Vector3DotProduct(f, nc);
  out[5].x = -f.x;
  out[5].y = -f.y;
  out[5].z = -f.z;
  out[5].w = Vector3DotProduct(f, fc);

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

/** CPU BVH frustum cull → inst bitset + upload tex_prim_vis. */
static void mod_render_rc_ws_cull(ModRenderCtx *ctx) {
  // agent: composer-2.5 | 2026-08-13 | CPU cull tick upload vis | 4d378d
  ctx->ws_frustum_valid = false;
  if (!ctx->ws_cull_ready || !ctx->ws_cpu.prim_tex_ready || !ctx->ws_cpu.bvh_tex_ready) {
    ctx->ws_cpu.cull_valid = false;
    ctx->ws_cpu.chunk_vis_n = 0;
    return;
  }
  if (ctx->ws_cpu.prim_count <= 0) {
    ctx->ws_cpu.cull_valid = false;
    ctx->ws_cpu.cull_vis_n = 0;
    ctx->ws_cpu.chunk_vis_n = 0;
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
  // agent: grok-4.6 | 2026-08-21 | chunk cull after BVH | dad173
  ng_rc_ws_chunk_cull(&ctx->ws_cpu, planes);
  ng_rc_ws_upload_prim_vis(&ctx->ws_cpu, ctx->rt_prim_vis.texture);
}

static const int k_chunk_dirs[NG_RC_CASCADES_MAX] = {6, 24, 96};
static const int k_chunk_side[NG_RC_CASCADES_MAX] = {8, 4, 2};
static const float k_chunk_cell[NG_RC_CASCADES_MAX] = {1.0f, 2.0f, 4.0f};
static const float k_chunk_t0[NG_RC_CASCADES_MAX] = {0.05f, 1.0f, 4.0f};
static const float k_chunk_t1[NG_RC_CASCADES_MAX] = {1.0f, 4.0f, 64.0f};

static void mod_render_chunk_fill_casc(ModRenderCtx *ctx, int c, const float origin[3]) {
  // agent: grok-4.6 | 2026-08-21 | fill prim carrier no merge wipe | b8bc5a
  NgRcPassShader *pass = &ctx->rc_ws_chunk_fill;
  RenderTexture2D *dest = &ctx->rt_chunk_casc[c];
  const int nd = k_chunk_dirs[c];
  const int nside = k_chunk_side[c];
  const float cell = k_chunk_cell[c];
  // agent: grok-4.6 | 2026-08-21 | C2 t1 64 more steps | 04531e
  const int steps = (c == NG_RC_CASCADES_MAX - 1) ? 32 : 24;
  const int pc = ctx->ws_cpu.prim_count;
  const float sky[3] = {NG_RC_SKY.x, NG_RC_SKY.y, NG_RC_SKY.z};
  rlDisableColorBlend();
  BeginTextureMode(*dest);
  ClearBackground(BLACK);
  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_chunk_origin"), origin,
                 SHADER_UNIFORM_VEC3);
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_cell"), &cell,
                 SHADER_UNIFORM_FLOAT);
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_n_side"), &nside,
                 SHADER_UNIFORM_INT);
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_dir_count"), &nd,
                 SHADER_UNIFORM_INT);
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_max_steps"), &steps,
                 SHADER_UNIFORM_INT);
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_prim_count"), &pc,
                 SHADER_UNIFORM_INT);
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_t0"), &k_chunk_t0[c],
                 SHADER_UNIFORM_FLOAT);
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_t1"), &k_chunk_t1[c],
                 SHADER_UNIFORM_FLOAT);
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_sky"), sky,
                 SHADER_UNIFORM_VEC3);
  // agent: grok-4.6 | 2026-08-21 | full merge C1 ping SH atlas | 57ccd6
  {
    float sky_on = (c == NG_RC_CASCADES_MAX - 1) ? 1.0f : 0.0f;
    SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_sky_on"), &sky_on,
                   SHADER_UNIFORM_FLOAT);
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_prim");
    if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc, ctx->ws_cpu.tex_prim);
    }
  }
  // agent: grok-4.6 | 2026-08-21 | bind prev SH atlas fill | ad6dd7
  {
    const float ext = ng_rc_ws_chunk_extent();
    int loc = GetShaderLocation(pass->sh.handle, "ng_chunk_extent");
    if (loc >= 0) {
      SetShaderValue(pass->sh.handle, loc, &ext, SHADER_UNIFORM_FLOAT);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_atlas");
    if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc,
                            ctx->chunk_gi_ready ? ctx->rt_page_irr.texture : ctx->ws_cpu.tex_prim);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_page_hash");
    if (loc >= 0 && ctx->ws_cpu.page_hash_tex_ready) {
      SetShaderValueTexture(pass->sh.handle, loc, ctx->ws_cpu.tex_page_hash);
    } else if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc, ctx->ws_cpu.tex_prim);
    }
  }
  mod_render_ws_fs_draw(ctx->ws_cpu.tex_prim, dest->texture.width, dest->texture.height);
  EndShaderMode();
  EndTextureMode();
  rlEnableColorBlend();
}

/** Copy scratch cascade into page atlas strip. */
static void mod_render_chunk_store_page(ModRenderCtx *ctx, RenderTexture2D *atlas, Texture2D src,
                                       int page, int nd, int nh) {
  // agent: grok-4.6 | 2026-08-21 | persist C1 C2 world merge | dfbd55
  NgRcPassShader *pass = &ctx->rc_ws_chunk_store;
  if (!pass->ready || !atlas || atlas->id == 0) {
    return;
  }
  rlDisableColorBlend();
  BeginTextureMode(*atlas);
  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_page"), &page,
                 SHADER_UNIFORM_INT);
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_nd"), &nd,
                 SHADER_UNIFORM_INT);
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_src");
    if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc, src);
    }
  }
  DrawRectangle(page * nd, 0, nd, nh, WHITE);
  EndShaderMode();
  EndTextureMode();
  rlEnableColorBlend();
}

/** T-merge near with world parent atlas into dest. */
static void mod_render_chunk_merge_to(ModRenderCtx *ctx, int page, int c_near, Texture2D far_tex,
                                     RenderTexture2D *dest) {
  // agent: grok-4.6 | 2026-08-21 | persist C1 C2 world merge | dfbd55
  NgRcPassShader *pass = &ctx->rc_ws_chunk_merge;
  RenderTexture2D *near_rt = &ctx->rt_chunk_casc[c_near];
  const NgRcWsPage *pg = &ctx->ws_cpu.pages[page];
  const int nd = k_chunk_dirs[c_near];
  const int ndp = k_chunk_dirs[c_near + 1];
  const int nside = k_chunk_side[c_near];
  const int nside_p = k_chunk_side[c_near + 1];
  const float cell_n = k_chunk_cell[c_near];
  const float cell_p = k_chunk_cell[c_near + 1];
  float origin[3];
  float bmax[3];
  ng_rc_ws_chunk_aabb(pg->cx, pg->cy, pg->cz, origin, bmax);
  (void)bmax;
  const float cc[3] = {(float)pg->cx, (float)pg->cy, (float)pg->cz};
  rlDisableColorBlend();
  BeginTextureMode(*dest);
  ClearBackground(BLACK);
  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_n_side"), &nside,
                 SHADER_UNIFORM_INT);
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_n_side_p"), &nside_p,
                 SHADER_UNIFORM_INT);
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_dir_count"), &nd,
                 SHADER_UNIFORM_INT);
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_dir_count_p"), &ndp,
                 SHADER_UNIFORM_INT);
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_home_page"), &page,
                 SHADER_UNIFORM_INT);
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_chunk_origin"), origin,
                 SHADER_UNIFORM_VEC3);
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_chunk_cc"), cc,
                 SHADER_UNIFORM_VEC3);
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_cell_n"), &cell_n,
                 SHADER_UNIFORM_FLOAT);
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_cell_p"), &cell_p,
                 SHADER_UNIFORM_FLOAT);
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_near");
    if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc, near_rt->texture);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_far");
    if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc, far_tex);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_page_hash");
    if (loc >= 0 && ctx->ws_cpu.page_hash_tex_ready) {
      SetShaderValueTexture(pass->sh.handle, loc, ctx->ws_cpu.tex_page_hash);
    }
  }
  DrawRectangle(0, 0, dest->texture.width, dest->texture.height, WHITE);
  EndShaderMode();
  EndTextureMode();
  rlEnableColorBlend();
}

static void mod_render_chunk_encode_page(ModRenderCtx *ctx, int page) {
  NgRcPassShader *pass = &ctx->rc_ws_chunk_encode;
  const int nd = k_chunk_dirs[0];
  const Texture2D atlas =
      ctx->rc_ws_chunk_merge.ready ? ctx->rt_chunk_merge.texture : ctx->rt_chunk_casc[0].texture;
  rlDisableColorBlend();
  BeginTextureMode(ctx->rt_page_irr);
  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_dir_count"), &nd,
                 SHADER_UNIFORM_INT);
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_page"), &page,
                 SHADER_UNIFORM_INT);
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_atlas");
    if (loc >= 0) {
      SetShaderValueTexture(pass->sh.handle, loc, atlas);
    }
  }
  DrawRectangle(page * 8, 0, 8, 256, WHITE);
  EndShaderMode();
  EndTextureMode();
  rlEnableColorBlend();
}

static void mod_render_chunk_fill_page(ModRenderCtx *ctx, int page) {
  const NgRcWsPage *p = &ctx->ws_cpu.pages[page];
  float bmin[3];
  float bmax[3];
  ng_rc_ws_chunk_aabb(p->cx, p->cy, p->cz, bmin, bmax);
  (void)bmax;
  for (int c = NG_RC_CASCADES_MAX - 1; c >= 0; c--) {
    mod_render_chunk_fill_casc(ctx, c, bmin);
  }
  // agent: grok-4.6 | 2026-08-21 | atlas dirs 6 24 96 | d748ce
  mod_render_chunk_store_page(ctx, &ctx->rt_page_c2, ctx->rt_chunk_casc[2].texture, page,
                             k_chunk_dirs[2], 8);
  if (ctx->rc_ws_chunk_merge.ready) {
    mod_render_chunk_merge_to(ctx, page, 1, ctx->rt_page_c2.texture, &ctx->rt_chunk_merge_c1);
    mod_render_chunk_store_page(ctx, &ctx->rt_page_c1, ctx->rt_chunk_merge_c1.texture, page, 24, 64);
    mod_render_chunk_merge_to(ctx, page, 0, ctx->rt_page_c1.texture, &ctx->rt_chunk_merge);
  }
  {
    const Texture2D c0 =
        ctx->rc_ws_chunk_merge.ready ? ctx->rt_chunk_merge.texture : ctx->rt_chunk_casc[0].texture;
    mod_render_chunk_store_page(ctx, &ctx->rt_page_c0, c0, page, k_chunk_dirs[0], 512);
  }
  mod_render_chunk_encode_page(ctx, page);
}

/** Fill dirty vis pages (cap per frame). */
static void mod_render_chunk_gi_fill(ModRenderCtx *ctx) {
  // agent: grok-4.6 | 2026-08-21 | chunk GI tick compose | 0d697a
  if (!ctx->chunk_gi_ready || !ctx->rc_ws_chunk_fill.ready || !ctx->ws_cpu.prim_tex_ready) {
    return;
  }
  const uint32_t h = ng_rc_ws_scene_hash();
  if (h != ctx->chunk_gi_scene_hash) {
    ng_rc_ws_pages_mark_dirty(&ctx->ws_cpu);
    ctx->chunk_gi_scene_hash = h;
  }
  int nfill = 0;
  for (int i = 0; i < NG_RC_WS_PAGE_CAP && nfill < NG_RC_WS_PAGE_FILL_MAX; i++) {
    NgRcWsPage *p = &ctx->ws_cpu.pages[i];
    if (!p->occupied || !p->dirty) {
      continue;
    }
    if (p->last_use != ctx->ws_cpu.page_tick) {
      continue;
    }
    mod_render_chunk_fill_page(ctx, i);
    p->dirty = 0;
    nfill++;
  }
}

static void mod_render_chunk_gi_resolve(ModRenderCtx *ctx) {
  if (!ctx->chunk_gi_ready || !ctx->rc_ws_chunk_resolve.ready || !ctx->gbuf_ready) {
    return;
  }
  NgRcPassShader *pass = &ctx->rc_ws_chunk_resolve;
  const int ping = ctx->ws_ping & 1;
  int pw = 0;
  int ph = 0;
  mod_render_internal_size(ctx, &pw, &ph);
  const float res[2] = {(float)pw, (float)ph};
  const float ext = ng_rc_ws_chunk_extent();
  rlDisableColorBlend();
  BeginTextureMode(ctx->rt_ws[ping]);
  ClearBackground(BLACK);
  BeginShaderMode(pass->sh.handle);
  ng_shader_set_common(&pass->sh, (float)GetTime());
  if (pass->sh.loc_resolution >= 0) {
    SetShaderValue(pass->sh.handle, pass->sh.loc_resolution, res, SHADER_UNIFORM_VEC2);
  }
  SetShaderValue(pass->sh.handle, GetShaderLocation(pass->sh.handle, "ng_chunk_extent"), &ext,
                 SHADER_UNIFORM_FLOAT);
  if (pass->loc_tex_depth >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_depth, ctx->rt_depth.texture);
  }
  if (pass->loc_tex_normal >= 0) {
    SetShaderValueTexture(pass->sh.handle, pass->loc_tex_normal, ctx->rt_normal.texture);
  }
  // agent: grok-4.6 | 2026-08-21 | bind hash cap two fills | 522970
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_page_hash");
    if (loc >= 0 && ctx->ws_cpu.page_hash_tex_ready) {
      SetShaderValueTexture(pass->sh.handle, loc, ctx->ws_cpu.tex_page_hash);
    }
  }
  {
    int loc = GetShaderLocation(pass->sh.handle, "tex_atlas");
    if (loc >= 0) {
      // agent: grok-4.6 | 2026-08-21 | persist merged C0 dir atlas | 7faa50
      SetShaderValueTexture(pass->sh.handle, loc, ctx->rt_page_c0.texture);
    }
  }
  mod_render_fs_draw(ctx->rt_depth.texture, pw, ph);
  EndShaderMode();
  EndTextureMode();
  rlEnableColorBlend();
}

// agent: grok-4.6 | 2026-08-21 | strip dead clip SS probe paths | 36a296
static void mod_render_rc_gpu_tick(ModRenderCtx *ctx) {
  const int force = mod_render_rc_ws_vox_dirty(ctx) ? 1 : 0;
  if (force || ctx->ws_cpu.prim_count <= 0) {
    ng_rc_ws_rebuild_prims(&ctx->ws_cpu);
  }
  mod_render_rc_ws_cull(ctx);
  mod_render_chunk_gi_fill(ctx);
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
  if (!ctx->blit_rgb_ready) {
    // agent: grok-4.6 | 2026-08-21 | gbuf debug blit rgb shader | e51d4f
    ctx->blit_rgb =
        ng_shader_load(NG_RES_ROOT "shaders/fullscreen.vs", NG_RES_ROOT "shaders/blit_rgb.fs");
    if (ctx->blit_rgb.handle.id == 0) {
      return false;
    }
    ctx->blit_rgb_ready = true;
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

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
/** @param ctx ModRenderCtx* cache. @param base const RenderAsset* geometry. @param id NgMaterialHandle override/default. @return RenderAsset cached material combined with geometry. */
static RenderAsset mod_render_material_asset(ModRenderCtx *ctx,const RenderAsset *base,NgMaterialHandle id) {
  const NgSceneMaterialDesc *recipe=ng_material_get(id);
  if(!recipe || recipe->font_src[0]) return *base;
  int i=0; while(i<ctx->material_count && ctx->material_ids[i]!=id.id) i++;
  if(i==ctx->material_count) {
    if(i>=NG_SCENE_ASSET_MAX*2) return *base;
    NgSceneResolvedModel r=recipe->recipe;
    r.mesh_kind=NG_SCENE_MESH_CUBE; r.mesh_w=r.mesh_h=r.mesh_d=1;
    char vs[160],fs[160]; snprintf(vs,sizeof(vs),NG_RES_ROOT "%s",r.vertex);
    snprintf(fs,sizeof(fs),NG_RES_ROOT "%s",r.fragment);
    mod_render_load_asset_mesh(&ctx->material_cache[i],&r,fs,vs,false);
    ctx->material_ids[i]=id.id; ctx->material_count++;
  }
  RenderAsset out=ctx->material_cache[i];
  out.model.meshes=base->model.meshes; out.model.meshCount=base->model.meshCount;
  return out;
}

/** @param a const RenderAsset* material/geometry. @param b NgInstanceBatch* instances. @return void. */
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
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  ng_shape_draw(a->model.meshes[0], a->model.materials[0], b->mats, b->count,
    MatrixMultiply(rlGetMatrixModelview(),rlGetMatrixProjection()));
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
/** @param ctx ModRenderCtx* renderer. @param a const RenderAsset* geometry/material. @param b NgInstanceBatch* instances. @param mode int gbuffer channel. @return void. */
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
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  const NgSceneMaterialDesc *recipe=ng_material_get(b->material);
  if(recipe) {
    if(recipe->depth_test) rlEnableDepthTest(); else rlDisableDepthTest();
    if(recipe->depth_write) rlEnableDepthMask(); else rlDisableDepthMask();
  }
  ng_shape_draw(a->model.meshes[0], mat, b->mats, b->count,
    MatrixMultiply(rlGetMatrixModelview(),rlGetMatrixProjection()));
  rlEnableDepthTest(); rlEnableDepthMask();
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
// agent: gpt-6-astra | 2026-09-05 | collect shared draw commands into batches | 9a9146
/** @param ctx ModRenderCtx* renderer. @param scope_id uint8_t pass scope. @return void; frame-draw step 3 reuses the immutable queue. */
static void mod_render_collect_graph_batches(ModRenderCtx *ctx, uint8_t scope_id) {
  mod_render_batches_reset_counts(ctx);
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  const NgDrawQueue *q = ng_draw_queue();
  const NgRcWsCtx *ws = &ctx->ws_cpu;
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  const bool skip_cull = ctx->debug_pass == NG_RENDER_PASS_CULLING ||
    ctx->debug_pass == NG_RENDER_PASS_GRID || ctx->debug_pass == NG_RENDER_PASS_UVW ||
    ctx->debug_pass == NG_RENDER_PASS_PROBES || ctx->debug_pass == NG_RENDER_PASS_PROBES_LOD;
  for (int i = 0; i < q->count; i++) {
    const NgDrawCommand *c = &q->commands[i];
    const NgDrawOptions *o = &c->options;
    if (c->kind != NG_DRAW_SHAPE || o->scope_id != scope_id) continue;
    if (!skip_cull && c->graph_index >= 0 && ws->cull_valid &&
        !ng_rc_ws_inst_visible(ws, c->graph_index)) continue;
    if (!mod_render_asset_for_model(ctx, c->model)) continue;
    NgInstanceBatch *b = mod_render_batch_get(ctx, c->model, o->tint, o->material);
    Quaternion rotation = QuaternionFromEuler(o->rotation[0], o->rotation[1], o->rotation[2]);
    Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(o->scale[0], o->scale[1], o->scale[2]),
      QuaternionToMatrix(rotation)), MatrixTranslate(o->position[0], o->position[1], o->position[2]));
    m.m15 = (float)c->graph_index;
    mod_render_batch_push(b, m);
  }
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
// agent: gpt-6-astra | 2026-09-05 | apply tint in forward shape batches | c821ae
/** @param ctx ModRenderCtx* renderer. @param alpha_only bool post-composition pass. @return void; flush material batches. */
static void mod_render_flush_batches(ModRenderCtx *ctx, bool alpha_only) {
  for(int transparent=alpha_only?1:0;transparent<2;transparent++)
  for (int i = 0; i < ctx->batch_count; i++) {
    NgInstanceBatch *b = &ctx->batches[i];
    if (b->count <= 0) {
      continue;
    }
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
    if(b->transparent != (bool)transparent) continue;
    RenderAsset *a = mod_render_cache_get(ctx, b->model);
    if (a) {
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
      RenderAsset styled=mod_render_material_asset(ctx,a,b->material);
      const NgSceneMaterialDesc *recipe=ng_material_get(b->material);
      if(recipe) {
        if(recipe->blend) rlEnableColorBlend(); else rlDisableColorBlend();
        if(recipe->depth_test) rlEnableDepthTest(); else rlDisableDepthTest();
        if(recipe->depth_write) rlEnableDepthMask(); else rlDisableDepthMask();
      }
      mod_render_draw_batch(&styled, b);
      rlEnableColorBlend(); rlEnableDepthTest(); rlEnableDepthMask();
    }
  }
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
// agent: gpt-6-astra | 2026-09-05 | apply tint in gbuffer shape batches | 6097fb
/** @param ctx ModRenderCtx* renderer. @param mode int gbuffer channel. @return void. */
static void mod_render_flush_batches_gbuf(ModRenderCtx *ctx, int mode) {
  for (int i = 0; i < ctx->batch_count; i++) {
    NgInstanceBatch *b = &ctx->batches[i];
    if (b->count <= 0) {
      continue;
    }
    RenderAsset *a = mod_render_cache_get(ctx, b->model);
    if (a) {
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
      if(b->transparent) continue;
      RenderAsset styled=mod_render_material_asset(ctx,a,b->material);
      mod_render_draw_batch_gbuf(ctx, &styled, b, mode);
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

/** Blit gbuf RGB; shader drops packed metal/rough so A cannot fake-light. */
static void mod_render_blit_rt_opaque(ModRenderCtx *ctx, const RenderTexture2D *rt) {
  // agent: grok-4.6 | 2026-08-21 | gbuf debug blit rgb shader | e51d4f
  if (ctx && ctx->blit_rgb_ready) {
    BeginShaderMode(ctx->blit_rgb.handle);
    mod_render_blit_rt(rt);
    EndShaderMode();
    return;
  }
  rlDisableColorBlend();
  mod_render_blit_rt(rt);
  rlEnableColorBlend();
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
/** @param ctx ModRenderCtx* renderer. @return void; scoped opaque, transparent and label passes. */
static void mod_render_draw_scene_graph(ModRenderCtx *ctx) {
  // agent: composer-2.5 | 2026-08-09 | instanced draw batch pools | 8837bc
  mod_scene_runtime_use_view();
  const uint8_t world_id = (uint8_t)mod_scene_assets_world_scope_id();
  mod_render_collect_graph_batches(ctx, world_id);
  BeginMode3D(ctx->camera);
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  mod_render_flush_batches(ctx,false);
  // agent: grok-4.6 | 2026-08-30 | batch draw by scope_id | 43b1e0
  // font-msdf step 8
  mod_font_msdf_draw_world(&ctx->camera, world_id);
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

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
/** @param ctx ModRenderCtx* renderer. @return void; collect legacy snapshots with default materials. */
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
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
    NgInstanceBatch *b = mod_render_batch_get(ctx, key, NULL, (NgMaterialHandle){0});
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

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
// agent: gpt-6-astra | 2026-09-05 | build one queue per rendered frame | 9c7346
/** @param ctx ModRenderCtx* renderer. @return void; owns frame-draw steps 1–5. */
/** @param ctx ModRenderCtx* renderer. @return void; frame-draw steps 1–5 with post-composition labels. */
static void mod_render_draw_scene(ModRenderCtx *ctx) {
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  mod_scene_draw();
  ng_draw_collect_graph(GetTime());
  ClearBackground(BLACK);
  mod_render_update_camera(ctx);

  if (!mod_render_ensure_present(ctx)) {
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
// agent: gpt-6-astra | 2026-09-05 | discard frame when render target unavailable | 761a13
    mod_render_draw_overlay(mod_render_authoritative_label(ctx), 10);
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
    ng_draw_reset();
    return;
  }

  mod_scene_runtime_use_view();
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
// agent: gpt-6-astra | 2026-09-05 | include immediate only scenes in render passes | 9906db
  const bool graph = ng_draw_queue()->count > 0 || mod_scene_view_graph_active() ||
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
// agent: grok-4.6 | 2026-08-21 | strip dead clip SS probe paths | 36a296
      bool rc_ready = false;
      if (want_rc && mod_render_ensure_rc(ctx)) {
        mod_render_rc_gpu_tick(ctx);
        rc_ready = ctx->ws_rt_ready && ctx->chunk_gi_ready && ctx->rc_compose.ready &&
                   ctx->ws_cpu.prim_tex_ready && ctx->ws_cpu.bvh_tex_ready && ctx->ws_cull_ready;
      }
      mod_render_collect_graph_batches(ctx, (uint8_t)mod_scene_assets_world_scope_id());
      mod_render_fill_gbuf_graph(ctx, &ctx->rt_albedo, 0);
      mod_render_fill_gbuf_graph(ctx, &ctx->rt_normal, 1);
      mod_render_fill_gbuf_graph(ctx, &ctx->rt_glow, 2);
      mod_render_fill_gbuf_graph(ctx, &ctx->rt_depth, 3);
      if (rc_ready) {
        mod_render_chunk_gi_resolve(ctx);
      }

      if (ctx->debug_pass != NG_RENDER_PASS_FINAL || want_rc) {
        BeginTextureMode(ctx->rt_present);
        if (ctx->debug_pass == NG_RENDER_PASS_ALBEDO) {
          ClearBackground(BLACK);
          mod_render_blit_rt_opaque(ctx, &ctx->rt_albedo);
        } else if (ctx->debug_pass == NG_RENDER_PASS_NORMAL) {
          ClearBackground(BLACK);
          mod_render_blit_rt_opaque(ctx, &ctx->rt_normal);
        } else if (ctx->debug_pass == NG_RENDER_PASS_GLOW) {
          ClearBackground(BLACK);
          mod_render_blit_rt(&ctx->rt_glow);
        } else if (ctx->debug_pass == NG_RENDER_PASS_DEPTH) {
          ClearBackground(BLACK);
          mod_render_blit_rt(&ctx->rt_depth);
        } else if (ctx->debug_pass == NG_RENDER_PASS_IRRADIANCE && rc_ready) {
          ClearBackground(BLACK);
          mod_render_blit_rt_opaque(ctx, &ctx->rt_ws[ctx->ws_ping & 1]);
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
          mod_render_rc_ws_debug(ctx, 0);
        } else if (ctx->debug_pass == NG_RENDER_PASS_GRID && rc_ready) {
          ClearBackground(BLACK);
          mod_render_rc_ws_debug(ctx, 2);
        } else if (ctx->debug_pass == NG_RENDER_PASS_ATLAS && rc_ready) {
          ClearBackground(BLACK);
          if (ctx->chunk_gi_ready) {
            const Texture2D t = ctx->rt_page_irr.texture;
            const float dw = (float)GetRenderWidth();
            const float dh = (float)GetRenderHeight();
            const Rectangle src = {0.0f, 0.0f, (float)t.width, -64.0f};
            DrawTexturePro(t, src, (Rectangle){0.0f, 0.0f, dw, dh}, (Vector2){0.0f, 0.0f}, 0.0f,
                           WHITE);
          }
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
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
// agent: gpt-6-astra | 2026-09-05 | render queued visuals without graph entities | d60cec
    if (ng_draw_queue()->count > 0 || mod_scene_view_graph_active()) {
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

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
// agent: gpt-6-astra | 2026-09-05 | include queued labels after RC composition | f04457
  if (composed && ctx->debug_pass == NG_RENDER_PASS_FINAL) {
    BeginTextureMode(ctx->rt_present);
    BeginMode3D(ctx->camera);
    mod_render_flush_batches(ctx,true);
    mod_font_msdf_draw_world(&ctx->camera, (uint8_t)mod_scene_assets_world_scope_id());
    EndMode3D();
    EndTextureMode();
  }
  mod_render_present_to_screen(ctx);
  mod_render_draw_overlay(mod_render_authoritative_label(ctx), 10);
  DrawText(TextFormat("scale=%.2f %dx%d", ctx->render_scale, ctx->present_w, ctx->present_h), 10,
           34, 18, LIME);
  // agent: grok-4.6 | 2026-08-30 | batch draw by scope_id | 43b1e0
  uint8_t ortho_id = 0;
  if (mod_scene_assets_ortho_scope_id(&ortho_id)) {
    rlDisableDepthTest();
    rlDisableBackfaceCulling();
    rlSetMatrixProjection(MatrixOrtho(0.0, (double)GetScreenWidth(), (double)GetScreenHeight(),
                                      0.0, -1.0, 1.0));
    rlSetMatrixModelview(MatrixIdentity());
    mod_render_collect_graph_batches(ctx, ortho_id);
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
    mod_render_flush_batches(ctx,false);
    mod_font_msdf_draw_screen(ortho_id);
    rlEnableBackfaceCulling();
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
// agent: gpt-6-astra | 2026-09-05 | consume transient queue after all passes | fac703
    rlEnableDepthTest();
  }
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
  mod_scene_runtime_use_view();
  ng_draw_reset(); /* frame-draw step 5 */
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

// agent: grok-4.6 | 2026-08-21 | strip dead clip SS probe paths | 36a296
static bool mod_render_init(void *vctx) {
  ModRenderCtx *ctx = (ModRenderCtx *)vctx;
  memset(ctx, 0, sizeof(*ctx));
  ctx->scene_label[0] = '\0';
  ctx->debug_pass = NG_RENDER_PASS_FINAL;
  ctx->rc_quality = 2;
  ctx->gi_strength = 1.0f;
  ctx->render_scale = 1.0f;
  ng_rc_ws_init(&ctx->ws_cpu);
  mod_render_init_camera(ctx);
  return true;
}

static void mod_render_shutdown(void *vctx) {
  ModRenderCtx *ctx = (ModRenderCtx *)vctx;
  mod_render_clear_cache(ctx);
  mod_render_unload_gbuf(ctx);
  mod_render_unload_rc(ctx);
  mod_render_unload_present(ctx);
  // agent: grok-4.6 | 2026-08-30 | drop fonts scene C branch | 347d96
  mod_font_msdf_shutdown();
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

// agent: grok-4.6 | 2026-08-21 | strip dead clip SS probe paths | 36a296
void mod_render_probe_snapshot_text(char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  const ModRenderCtx *ctx = &g_render_ctx;
  int pages = 0;
  for (int i = 0; i < NG_RC_WS_PAGE_CAP; i++) {
    if (ctx->ws_cpu.pages[i].occupied) {
      pages++;
    }
  }
  snprintf(out, cap, "pages used=%d vis_chunks=%d prims=%d", pages, ctx->ws_cpu.chunk_vis_n,
           ctx->ws_cpu.prim_count);
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
  if (strcmp(path, "debug.logging.probes") == 0) {
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
  if (strcmp(path, "debug.logging.probes") == 0) {
    snprintf(out, cap, "0");
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

// agent: grok-4.6 | 2026-08-31 | screenshot request API | 9c40fa
static char g_screenshot_path[512];

void mod_render_request_screenshot(const char *path) {
  if (!path || !path[0]) {
    return;
  }
  strncpy(g_screenshot_path, path, sizeof(g_screenshot_path) - 1);
  g_screenshot_path[sizeof(g_screenshot_path) - 1] = '\0';
}

void mod_render_flush_screenshot(void) {
  if (g_screenshot_path[0] == '\0') {
    return;
  }
  TakeScreenshot(g_screenshot_path);
  g_screenshot_path[0] = '\0';
}
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0ddae0
// agent: gpt-6-astra | 2026-09-05 | size draw caches for shared queue | 2614ef
// agent: gpt-6-astra | 2026-09-05 | batch shape material tint | eeba45
// agent: gpt-6-astra | 2026-09-05 | key shape batches by tint | 544c59
// agent: gpt-6-astra | 2026-09-05 | resolve shared unit physics meshes | 57e026
// agent: gpt-6-astra | 2026-09-05 | load unit geometry for physics draws | 845554
// agent: gpt-6-astra | 2026-09-05 | collect shared draw commands into batches | 9a9146
// agent: gpt-6-astra | 2026-09-05 | apply tint in forward shape batches | c821ae
// agent: gpt-6-astra | 2026-09-05 | apply tint in gbuffer shape batches | 6097fb
// agent: gpt-6-astra | 2026-09-05 | build one queue per rendered frame | 9c7346
// agent: gpt-6-astra | 2026-09-05 | discard frame when render target unavailable | 761a13
// agent: gpt-6-astra | 2026-09-05 | include queued labels after RC composition | f04457
// agent: gpt-6-astra | 2026-09-05 | consume transient queue after all passes | fac703
// agent: gpt-6-astra | 2026-09-05 | include immediate only scenes in render passes | 9906db
// agent: gpt-6-astra | 2026-09-05 | render queued visuals without graph entities | d60cec
// agent: gpt-6-astra | 2026-09-05 | free retained batch scratch slots | 5cef5e
// agent: gpt-6-astra | 2026-09-05 | reuse batch slots across animated tints | 59630c
// agent: gpt-6-astra | 2026-09-05 | release scene font cache with meshes | 624d9e

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
// agent: grok-4.6 | 2026-08-21 | unhook sparse probe tick | 7ddeb3
// agent: grok-4.6 | 2026-08-21 | chunk cull after BVH | dad173
// agent: grok-4.6 | 2026-08-21 | debug bind visible chunks | 82bbfc
// agent: grok-4.6 | 2026-08-21 | grid debug fs_draw Y flip | 5de069
// agent: grok-4.6 | 2026-08-21 | uvw grid fs_draw chunks[0] | 4a930b
// agent: grok-4.6 | 2026-08-21 | restore camera-basis frustum planes | 75af91
// agent: grok-4.6 | 2026-08-21 | grid debug skip cull filter | 590e0e
// agent: grok-4.6 | 2026-08-21 | bind chunk vis texture debug | 8b501b
// agent: grok-4.6 | 2026-08-21 | opaque blit gbuf debug | 48f37a
// agent: grok-4.6 | 2026-08-21 | gbuf debug blit rgb shader | e51d4f
// agent: grok-4.6 | 2026-08-21 | bind tex_pages probes debug | 9920e2
// agent: grok-4.6 | 2026-08-21 | chunk GI tick compose | 0d697a
// agent: grok-4.6 | 2026-08-21 | merge ping matches c0 atlas | b8c8b2
// agent: grok-4.6 | 2026-08-21 | fill prim carrier no merge wipe | b8bc5a
// agent: grok-4.6 | 2026-08-21 | bind hash cap two fills | 522970
// agent: grok-4.6 | 2026-08-21 | full merge C1 ping SH atlas | 57ccd6
// agent: grok-4.6 | 2026-08-21 | persist C1 C2 world merge | dfbd55
// agent: grok-4.6 | 2026-08-21 | atlas dirs 6 24 96 | d748ce
// agent: grok-4.6 | 2026-08-21 | persist merged C0 dir atlas | 7faa50
// agent: grok-4.6 | 2026-08-21 | C2 t1 64 more steps | 04531e
// agent: grok-4.6 | 2026-08-21 | strip dead clip SS probe paths | 36a296
// agent: grok-4.6 | 2026-08-21 | bind prev SH atlas fill | ad6dd7
// agent: grok-4.6 | 2026-08-30 | drop fonts scene C branch | 347d96
// agent: grok-4.6 | 2026-08-30 | batch draw by scope_id | 43b1e0
// agent: grok-4.6 | 2026-08-31 | load bind albedo texture | 85e565
// agent: grok-4.6 | 2026-08-31 | flip albedo image vertical | 1576d2
