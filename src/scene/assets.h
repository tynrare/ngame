// agent: composer-2.5 | 2026-07-28 | js-driven scene asset registry | c1d2e3
// agent: composer-2.5 | 2026-08-09 | shader glow rough metal uniforms | 9bd320
// agent: composer-2.5 | 2026-08-09 | scene render mode enum | d6a4aa
// agent: composer-2.5 | 2026-08-09 | set view camera assets | 10114b
#ifndef MOD_SCENE_ASSETS_H
#define MOD_SCENE_ASSETS_H

#include "world/ng_world.h"
#include <stdbool.h>
#include <stdint.h>

#define NG_SCENE_ASSET_MAX 32

typedef enum NgSceneMeshKind {
  NG_SCENE_MESH_CUBE = 0,
  NG_SCENE_MESH_SPHERE = 1,
} NgSceneMeshKind;

typedef enum NgSceneCameraMode {
  NG_SCENE_CAM_FIXED = 0,
  NG_SCENE_CAM_ORBIT = 1,
  // agent: grok-4.6 | 2026-08-30 | named scope table APIs | 870861
  NG_SCENE_CAM_ORTHO = 2,
} NgSceneCameraMode;

typedef enum NgSceneRenderMode {
  NG_SCENE_RENDER_SIMPLE = 0,
  NG_SCENE_RENDER_GBUFFER = 1,
  NG_SCENE_RENDER_RC = 2,
} NgSceneRenderMode;

// agent: grok-4.6 | 2026-08-30 | font model draw src fields | 8b11df
typedef enum NgSceneModelDraw {
  NG_SCENE_DRAW_MESH = 0,
  NG_SCENE_DRAW_MSDF = 1,
} NgSceneModelDraw;

typedef struct NgSceneMeshDesc {
  bool alive;
  char name[32];
  NgSceneMeshKind kind;
  float width;
  float height;
  float depth;
} NgSceneMeshDesc;

typedef struct NgSceneShaderDesc {
  bool alive;
  char name[32];
  char fragment[64];
  char vertex[64];
  bool have_tint;
  uint8_t tint_r;
  uint8_t tint_g;
  uint8_t tint_b;
  bool have_glow;
  uint8_t glow_r;
  uint8_t glow_g;
  uint8_t glow_b;
  float roughness;
  float metalness;
} NgSceneShaderDesc;

typedef struct NgSceneModelDesc {
  bool alive;
  char name[32];
  char mesh[32];
  char shader[32];
  NgSceneMeshKind mesh_kind;
  // agent: grok-4.6 | 2026-08-30 | font model draw src fields | 8b11df
  NgSceneModelDraw draw;
  char font_src[64];
} NgSceneModelDesc;

typedef struct NgSceneViewMeta {
  bool valid;
  uint8_t bg_r;
  uint8_t bg_g;
  uint8_t bg_b;
  NgSceneCameraMode camera_mode;
  NgSceneRenderMode render_mode;
  float cam_pos[3];
  float cam_target[3];
  float cam_fovy;
  float orbit_radius;
  float orbit_speed;
  float orbit_height;
} NgSceneViewMeta;

// agent: grok-4.6 | 2026-08-30 | named scope table APIs | 870861
typedef struct NgSceneScopeDesc {
  bool alive;
  char name[32];
  NgSceneViewMeta meta;
} NgSceneScopeDesc;

typedef struct NgSceneResolvedModel {
  bool ok;
  NgSceneMeshKind mesh_kind;
  float mesh_w;
  float mesh_h;
  float mesh_d;
  char fragment[64];
  char vertex[64];
  bool have_tint;
  uint8_t tint_r;
  uint8_t tint_g;
  uint8_t tint_b;
  bool have_glow;
  uint8_t glow_r;
  uint8_t glow_g;
  uint8_t glow_b;
  float roughness;
  float metalness;
} NgSceneResolvedModel;

void mod_scene_assets_reset(void);
bool mod_scene_assets_describe_mesh(const char *name, const char *shape, float w, float h,
                                    float d);
bool mod_scene_assets_describe_shader(const char *name, const char *fragment, const char *vertex,
                                      uint8_t tint_r, uint8_t tint_g, uint8_t tint_b,
                                      bool have_tint, uint8_t glow_r, uint8_t glow_g,
                                      uint8_t glow_b, bool have_glow, float roughness,
                                      float metalness);
bool mod_scene_assets_describe_model(const char *name, const char *mesh, const char *shader);
/** Register an MSDF font as a model (no mesh). */
bool mod_scene_assets_describe_font(const char *name, const char *src);
const NgSceneModelDesc *mod_scene_assets_get_model(const char *name);
/** First MSDF font src, or NULL. */
const char *mod_scene_assets_first_font_src(void);
bool mod_scene_assets_describe_view(const NgSceneViewMeta *view);
/** Register a named camera/render scope. */
bool mod_scene_assets_describe_scope(const char *name, const NgSceneViewMeta *view);
/** Bind scene to named scopes (non-legacy). */
bool mod_scene_assets_bind_scene_scopes(const char *const *names, int n);
bool mod_scene_assets_legacy_scopes(void);
int mod_scene_assets_lookup_scope(const char *name);
int mod_scene_assets_default_scope_id(void);
int mod_scene_assets_world_scope_id(void);
bool mod_scene_assets_ortho_scope_id(uint8_t *out_id);
const NgSceneScopeDesc *mod_scene_assets_scope_at(int id);
/** Update view camera pos/target (NULL skips). Forces fixed mode. */
bool mod_scene_assets_set_view_camera(const float *pos, const float *target);
bool mod_scene_assets_dispose(const char *kind, const char *name);
bool mod_scene_assets_resolve_model(const char *model_name, NgSceneResolvedModel *out);
bool mod_scene_assets_resolve_model_for_mesh_kind(NgSceneMeshKind kind, NgSceneResolvedModel *out);
const NgSceneViewMeta *mod_scene_assets_view(void);
NgEntityType mod_scene_assets_entity_type_for_kind(NgSceneMeshKind kind);

#endif

// agent: composer-2.5 | 2026-07-28 | js-driven scene asset registry | c1d2e3
// agent: composer-2.5 | 2026-08-09 | shader glow rough metal uniforms | 9bd320
// agent: composer-2.5 | 2026-08-09 | scene render mode enum | d6a4aa
// agent: composer-2.5 | 2026-08-09 | set view camera assets | 10114b
// agent: grok-4.6 | 2026-08-30 | font model draw src fields | 8b11df
// agent: grok-4.6 | 2026-08-30 | named scope table APIs | 870861
