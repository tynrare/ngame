// agent: composer-2.5 | 2026-07-28 | js-driven scene asset registry | c1d2e3
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
} NgSceneCameraMode;

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
} NgSceneShaderDesc;

typedef struct NgSceneModelDesc {
  bool alive;
  char name[32];
  char mesh[32];
  char shader[32];
  NgSceneMeshKind mesh_kind;
} NgSceneModelDesc;

typedef struct NgSceneViewMeta {
  bool valid;
  uint8_t bg_r;
  uint8_t bg_g;
  uint8_t bg_b;
  NgSceneCameraMode camera_mode;
  float cam_pos[3];
  float cam_target[3];
  float cam_fovy;
  float orbit_radius;
  float orbit_speed;
  float orbit_height;
} NgSceneViewMeta;

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
} NgSceneResolvedModel;

void mod_scene_assets_reset(void);
bool mod_scene_assets_describe_mesh(const char *name, const char *shape, float w, float h,
                                    float d);
bool mod_scene_assets_describe_shader(const char *name, const char *fragment, const char *vertex,
                                      uint8_t tint_r, uint8_t tint_g, uint8_t tint_b,
                                      bool have_tint);
bool mod_scene_assets_describe_model(const char *name, const char *mesh, const char *shader);
bool mod_scene_assets_describe_view(const NgSceneViewMeta *view);
bool mod_scene_assets_dispose(const char *kind, const char *name);
bool mod_scene_assets_resolve_model(const char *model_name, NgSceneResolvedModel *out);
bool mod_scene_assets_resolve_model_for_mesh_kind(NgSceneMeshKind kind, NgSceneResolvedModel *out);
const NgSceneViewMeta *mod_scene_assets_view(void);
NgEntityType mod_scene_assets_entity_type_for_kind(NgSceneMeshKind kind);

#endif

// agent: composer-2.5 | 2026-07-28 | js-driven scene asset registry | c1d2e3
