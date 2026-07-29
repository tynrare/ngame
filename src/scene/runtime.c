// agent: composer-2.5 | 2026-07-29 | scene runtime server view | 7c20e0
#include "scene/runtime.h"

NgSceneRuntime g_scene_server;
NgSceneRuntime g_scene_view;

static NgSceneRuntime *g_scene_active = &g_scene_server;

void mod_scene_runtime_use_server(void) { g_scene_active = &g_scene_server; }

void mod_scene_runtime_use_view(void) { g_scene_active = &g_scene_view; }

NgSceneRuntime *mod_scene_runtime_active(void) { return g_scene_active; }

ModSceneCtx *mod_scene_runtime_scene(void) { return &g_scene_active->scene; }

ModSceneGraphCtx *mod_scene_runtime_graph(void) { return &g_scene_active->graph; }

ModSceneAssetsCtx *mod_scene_runtime_assets(void) { return &g_scene_active->assets; }

// agent: composer-2.5 | 2026-07-29 | scene runtime server view | 7c20e0
