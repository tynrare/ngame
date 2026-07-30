// agent: composer-2.5 | 2026-07-25 | client render module | g0j28e
// agent: composer-2.5 | 2026-07-28 | render drop embedded path | f42f1c
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
#include "world/ng_world.h"
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <stdio.h>
#include <string.h>

typedef struct RenderAsset {
  bool ready;
  Model model;
  NgShader shader;
  Color bg;
  Color tint;
} RenderAsset;

#define NG_RENDER_CACHE_MAX 16

typedef struct RenderAssetCacheEntry {
  char key[32];
  RenderAsset asset;
} RenderAssetCacheEntry;

typedef struct ModRenderCtx {
  RenderAssetCacheEntry cache[NG_RENDER_CACHE_MAX];
  int cache_count;
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
} ModRenderCtx;

static ModRenderCtx g_render_ctx;

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

static void mod_render_clear_cache(ModRenderCtx *ctx) {
  for (int i = 0; i < ctx->cache_count; i++) {
    mod_render_unload_asset(&ctx->cache[i].asset);
  }
  ctx->cache_count = 0;
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
  DrawText(TextFormat("scene: %s", label ? label : "?"), 10, y, 20, RAYWHITE);
  const char *banner = mod_scene_native_banner();
  if (banner) {
    DrawText(banner, 10, y + 24, 18, YELLOW);
  }
}

static void mod_render_draw_entity_live(const RenderAsset *a, NgEntityType type, float x, float y,
                                        float z, const float rot[3], float scale, float phase) {
  (void)type;
  if (!a->ready || a->shader.handle.id == 0) {
    return;
  }
  const float client_t = (float)GetTime() + phase;
  ng_shader_set_common((NgShader *)&a->shader, client_t);
  if (a->shader.loc_tint >= 0) {
    const float tint[3] = {(float)a->tint.r / 255.0f, (float)a->tint.g / 255.0f,
                           (float)a->tint.b / 255.0f};
    SetShaderValue(a->shader.handle, a->shader.loc_tint, tint, SHADER_UNIFORM_VEC3);
  }

  // agent: composer-2.5 | 2026-07-29 | draw entities via model transform | 1415d8
  // Use described Model + transform (DrawModel). Local scale/rotate, then world translate.
  const float s = scale > 0.0f ? scale : 1.0f;
  Model model = a->model;
  model.transform = MatrixMultiply(
      MatrixMultiply(MatrixScale(s, s, s), MatrixRotateXYZ((Vector3){rot[0], rot[1], rot[2]})),
      MatrixTranslate(x, y, z));
  DrawModel(model, (Vector3){0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
}

static void mod_render_draw_graph_inst(ModRenderCtx *ctx, const NgSceneInst *inst) {
  RenderAsset *a = mod_render_asset_for_model(ctx, inst->model);
  if (!a) {
    return;
  }
  NgSceneResolvedModel resolved;
  NgEntityType type = NG_ENTITY_CUBE;
  if (mod_scene_assets_resolve_model(inst->model, &resolved) && resolved.ok) {
    type = mod_scene_assets_entity_type_for_kind(resolved.mesh_kind);
  }
  mod_render_draw_entity_live(a, type, inst->pos[0], inst->pos[1], inst->pos[2], inst->rot,
                              inst->scale, inst->phase);
}

static void mod_render_draw_scene_graph(ModRenderCtx *ctx) {
  mod_scene_runtime_use_view();
  const int n = mod_scene_graph_inst_count();
  BeginMode3D(ctx->camera);
  for (int i = 0; i < n; i++) {
    const NgSceneInst *inst = mod_scene_graph_inst_at(i);
    if (inst) {
      mod_render_draw_graph_inst(ctx, inst);
    }
  }
  EndMode3D();
}

static void mod_render_draw_waiting(ModRenderCtx *ctx) {
  ClearBackground(BLACK);
  char line[160];
  if (!mod_net_is_connected()) {
    snprintf(line, sizeof(line), "gateway: waiting for local server...");
  } else {
    snprintf(line, sizeof(line), "gateway: waiting for snapshot...");
  }
  (void)ctx;
  DrawText(line, 20, 20, 18, GRAY);
}

static void mod_render_draw_entity(const RenderAsset *a, const NgEntitySnap *e,
                                   const NgEntitySnap *p, float alpha) {
  // agent: composer-2.5 | 2026-07-25 | skip draw invalid shader | 9b4abd
  if (!a->ready || a->shader.handle.id == 0) {
    return;
  }
  const float client_t = (float)GetTime();
  const float yaw = mod_render_lerp(p ? p->rot_y : e->rot_y, e->rot_y, alpha);

  ng_shader_set_common((NgShader *)&a->shader, client_t);
  if (a->shader.loc_tint >= 0) {
    const float tint[3] = {(float)a->tint.r / 255.0f, (float)a->tint.g / 255.0f,
                           (float)a->tint.b / 255.0f};
    SetShaderValue(a->shader.handle, a->shader.loc_tint, tint, SHADER_UNIFORM_VEC3);
  }

  const Vector3 pos = {mod_render_lerp(p ? p->pos[0] : e->pos[0], e->pos[0], alpha),
                       mod_render_lerp(p ? p->pos[1] : e->pos[1], e->pos[1], alpha),
                       mod_render_lerp(p ? p->pos[2] : e->pos[2], e->pos[2], alpha)};
  if (e->type == NG_ENTITY_CUBE) {
    DrawModelEx(a->model, pos, (Vector3){0.0f, 1.0f, 0.0f}, yaw * 57.2958f,
                (Vector3){1.0f, 1.0f, 1.0f}, WHITE);
  } else {
    DrawModel(a->model, pos, 1.0f, WHITE);
  }
}

static void mod_render_draw_scene(ModRenderCtx *ctx) {
  ClearBackground(mod_render_bg_color());
  mod_render_update_camera(ctx);

  mod_scene_runtime_use_view();
  if (mod_scene_view_graph_active()) {
    mod_render_draw_scene_graph(ctx);
  } else if (ctx->have_curr && ctx->curr.entity_count > 0) {
    BeginMode3D(ctx->camera);
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
      const RenderAsset *a = mod_render_asset_for_mesh_kind(ctx, kind);
      if (a) {
        mod_render_draw_entity(a, e, p, ctx->alpha);
      }
    }
    EndMode3D();
  } else if (mod_scene_view_is_loaded()) {
    mod_render_draw_scene_graph(ctx);
  } else if (mod_net_is_authoritative() && mod_scene_is_loaded()) {
    mod_scene_runtime_use_server();
    mod_render_draw_scene_graph(ctx);
  } else if (ctx->have_curr) {
    BeginMode3D(ctx->camera);
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
      const RenderAsset *a = mod_render_asset_for_mesh_kind(ctx, kind);
      if (a) {
        mod_render_draw_entity(a, e, p, ctx->alpha);
      }
    }
    EndMode3D();
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
    if (ctx->have_curr || ctx->have_session || mod_scene_view_is_loaded() ||
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
  mod_render_init_camera(ctx);
  return true;
}

static void mod_render_shutdown(void *vctx) {
  ModRenderCtx *ctx = (ModRenderCtx *)vctx;
  mod_render_clear_cache(ctx);
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

// agent: composer-2.5 | 2026-07-29 | snapshot before empty graph | 57ca7b
// agent: composer-2.5 | 2026-07-29 | overlay label view authority | 6d7863
// agent: composer-2.5 | 2026-07-28 | render drop embedded path | f42f1c
// agent: composer-2.5 | 2026-07-29 | draw entities via model transform | 1415d8
// agent: composer-2.5 | 2026-07-29 | Extend NgModOps side fixed_step | 220dba
