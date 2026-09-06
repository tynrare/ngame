// agent: composer-2.5 | 2026-07-29 | dual scene runtime header | ce9266
// agent: composer-2.5 | 2026-07-29 | physics runtime ctx | d535f0
#ifndef NG_SCENE_RUNTIME_H
#define NG_SCENE_RUNTIME_H

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 5c9fb7
// agent: gpt-6-astra | 2026-09-05 | include frame draw types | 52a5a2
#include "scene/draw.h"
#include "scene/assets.h"
#include "scene/graph.h"
#include "scene/physics.h"
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
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 5c9fb7
  NgSceneMaterialDesc materials[NG_SCENE_ASSET_MAX*2];
  int material_count;
  NgSceneViewMeta view;
  // agent: grok-4.6 | 2026-08-30 | scopes in assets ctx | 667845
  NgSceneScopeDesc scopes[NG_SCENE_ASSET_MAX];
  int scope_count;
  uint8_t scene_scope_ids[NG_SCENE_ASSET_MAX];
  int scene_scope_count;
  bool legacy_scopes;
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
  // agent: composer-2.5 | 2026-08-01 | wired module instance slots | 44dab2
  int wire_count;
  char wire_ids[8][48];
} ModSceneCtx;

typedef struct NgSceneRuntime {
  ModSceneCtx scene;
  ModSceneGraphCtx graph;
  ModSceneAssetsCtx assets;
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 5c9fb7
// agent: gpt-6-astra | 2026-09-05 | isolate draw queues per runtime | 44ee7b
  ModScenePhysicsCtx physics;
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 5c9fb7
  NgDrawQueue draw;
} NgSceneRuntime;

extern NgSceneRuntime g_scene_server;
extern NgSceneRuntime g_scene_view;

void mod_scene_runtime_use_server(void);
void mod_scene_runtime_use_view(void);
NgSceneRuntime *mod_scene_runtime_active(void);
ModSceneCtx *mod_scene_runtime_scene(void);
ModSceneGraphCtx *mod_scene_runtime_graph(void);
ModSceneAssetsCtx *mod_scene_runtime_assets(void);
ModScenePhysicsCtx *mod_scene_runtime_physics(void);

#endif
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 5c9fb7
// agent: gpt-6-astra | 2026-09-05 | include frame draw types | 52a5a2
// agent: gpt-6-astra | 2026-09-05 | isolate draw queues per runtime | 44ee7b
// agent: composer-2.5 | 2026-07-29 | deferred js scene route support | 149fdb
// agent: composer-2.5 | 2026-07-29 | physics runtime ctx | d535f0
// agent: composer-2.5 | 2026-08-01 | wired module instance slots | 44dab2
// agent: grok-4.6 | 2026-08-30 | scopes in assets ctx | 667845
