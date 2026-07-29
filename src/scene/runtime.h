// agent: composer-2.5 | 2026-07-29 | dual scene runtime header | ce9266
#ifndef NG_SCENE_RUNTIME_H
#define NG_SCENE_RUNTIME_H

#include "scene/assets.h"
#include "scene/graph.h"
#include <stdbool.h>
#include <stdint.h>

struct duk_hthread;
typedef struct duk_hthread duk_context;

typedef struct ModSceneAssetsCtx {
  NgSceneMeshDesc meshes[NG_SCENE_ASSET_MAX];
  int mesh_count;
  NgSceneShaderDesc shaders[NG_SCENE_ASSET_MAX];
  int shader_count;
  NgSceneModelDesc models[NG_SCENE_ASSET_MAX];
  int model_count;
  NgSceneViewMeta view;
} ModSceneAssetsCtx;

typedef struct ModSceneCtx {
  duk_context *ctx;
  char scene_id[32];
  // agent: composer-2.5 | 2026-07-29 | deferred js scene route support | 149fdb
  char pending_scene_id[32];
  bool is_controller;
  bool is_server_host;
  bool loaded;
  bool inited;
  bool started;
  bool native;
  int scene_inst_stash;
} ModSceneCtx;

typedef struct NgSceneRuntime {
  ModSceneCtx scene;
  ModSceneGraphCtx graph;
  ModSceneAssetsCtx assets;
} NgSceneRuntime;

extern NgSceneRuntime g_scene_server;
extern NgSceneRuntime g_scene_view;

void mod_scene_runtime_use_server(void);
void mod_scene_runtime_use_view(void);
NgSceneRuntime *mod_scene_runtime_active(void);
ModSceneCtx *mod_scene_runtime_scene(void);
ModSceneGraphCtx *mod_scene_runtime_graph(void);
ModSceneAssetsCtx *mod_scene_runtime_assets(void);

#endif
// agent: composer-2.5 | 2026-07-29 | deferred js scene route support | 149fdb
