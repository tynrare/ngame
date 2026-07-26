// agent: composer-2.5 | 2026-07-25 | client render module | g0j28e
#include "mod_render.h"
#include "core/ng_bus.h"
#if defined(NG_HAS_EMBEDDED)
#include "core/ng_embed.h"
#endif
#include "core/ng_action.h"
#include "mod/mod_input.h"
#include "mod/mod_net.h"
#include "mod/mod_scene.h"
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

typedef struct ModRenderCtx {
  RenderAsset sphere;
  RenderAsset cube;
  NgSnapshot prev;
  NgSnapshot curr;
  bool have_prev;
  bool have_curr;
  float alpha;
  char scene_label[32];
  bool have_session;
  bool client_fields;
  Camera3D camera;
  uint16_t last_input_seq;
} ModRenderCtx;

static ModRenderCtx g_render_ctx;

static void mod_render_load_asset(RenderAsset *a, const char *mesh_kind,
                                  const char *fs_path, Color bg, Color tint) {
  if (a->ready) {
    return;
  }
  Mesh mesh;
  if (strcmp(mesh_kind, "sphere") == 0) {
    mesh = GenMeshSphere(1.0f, 32, 32);
  } else {
    mesh = GenMeshCube(1.5f, 1.5f, 1.5f);
  }
  a->model = LoadModelFromMesh(mesh);
  a->shader = ng_shader_load(NG_RES_ROOT "shaders/mesh.vs", fs_path);
  if (a->shader.handle.id == 0) {
    UnloadModel(a->model);
    return;
  }
  a->model.materials[0].shader = a->shader.handle;
  a->bg = bg;
  a->tint = tint;
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

static void mod_render_init_assets(ModRenderCtx *ctx) {
  mod_render_load_asset(&ctx->sphere, "sphere", NG_RES_ROOT "shaders/sphere.fs",
                        (Color){12, 20, 48, 255}, (Color){89, 140, 255, 255});
  mod_render_load_asset(&ctx->cube, "cube", NG_RES_ROOT "shaders/cube.fs",
                        (Color){48, 24, 8, 255}, (Color){255, 140, 51, 255});
  ctx->camera = (Camera3D){
      .position = (Vector3){3.0f, 2.0f, 5.0f},
      .target = (Vector3){0.0f, 0.0f, 0.0f},
      .up = (Vector3){0.0f, 1.0f, 0.0f},
      .fovy = 45.0f,
      .projection = CAMERA_PERSPECTIVE,
  };
}

static float mod_render_lerp(float a, float b, float t) { return a + (b - a) * t; }

static void mod_render_update_camera(ModRenderCtx *ctx, const char *scene) {
  // agent: composer-2.5 | 2026-07-25 | client-only camera hot path | b3d7e2
  const float client_t = (float)GetTime();
  const float yaw =
      (strcmp(scene, "cube") == 0 && mod_scene_client_fields_active()) ? mod_scene_get_rot_y()
                                                                        : mod_input_pred_yaw();

  if (strcmp(scene, "sphere") == 0) {
    const float radius = 6.0f;
    ctx->camera.position.x = sinf(client_t * 0.6f) * radius;
    ctx->camera.position.z = cosf(client_t * 0.6f) * radius;
    ctx->camera.position.y = 2.0f + sinf(client_t * 0.3f) * 0.5f;
    ctx->camera.target = (Vector3){0.0f, 0.0f, 0.0f};
  } else if (strcmp(scene, "cube") == 0) {
    const float dist = 6.0f;
    ctx->camera.position.x = sinf(yaw) * dist;
    ctx->camera.position.z = cosf(yaw) * dist;
    ctx->camera.position.y = 2.5f;
    ctx->camera.target = (Vector3){0.0f, 0.0f, 0.0f};
  }
}

static void mod_render_draw_entity_live(const RenderAsset *a, NgEntityType type, float x, float y,
                                        float z, float rot_y) {
  if (!a->ready || a->shader.handle.id == 0) {
    return;
  }
  const float client_t = (float)GetTime();
  ng_shader_set_common((NgShader *)&a->shader, client_t);
  if (a->shader.loc_tint >= 0) {
    const float tint[3] = {(float)a->tint.r / 255.0f, (float)a->tint.g / 255.0f,
                           (float)a->tint.b / 255.0f};
    SetShaderValue(a->shader.handle, a->shader.loc_tint, tint, SHADER_UNIFORM_VEC3);
  }

  const Vector3 pos = {x, y, z};
  if (type == NG_ENTITY_CUBE) {
    DrawModelEx(a->model, pos, (Vector3){0.0f, 1.0f, 0.0f}, rot_y * 57.2958f,
                (Vector3){1.0f, 1.0f, 1.0f}, WHITE);
  } else {
    DrawModel(a->model, pos, 1.0f, WHITE);
  }
}

// agent: composer-2.5 | 2026-07-25 | embedded shared world draw | a9f1c4
static void mod_render_draw_embedded(ModRenderCtx *ctx) {
  const NgWorld *w = ng_embed_world();
  if (!w || w->live_count <= 0) {
    ClearBackground(BLACK);
    DrawText("embedded: waiting for world...", 20, 20, 18, GRAY);
    return;
  }

  Color bg = (Color){0, 0, 0, 255};
  if (strcmp(w->scene_id, "sphere") == 0) {
    bg = ctx->sphere.bg;
  } else if (strcmp(w->scene_id, "cube") == 0) {
    bg = ctx->cube.bg;
  }
  ClearBackground(bg);
  mod_render_update_camera(ctx, w->scene_id);

  const float cube_yaw = mod_input_pred_yaw();
  BeginMode3D(ctx->camera);
  for (int i = 0; i < NG_WORLD_ENTITY_MAX; i++) {
    if (!w->alive[i]) {
      continue;
    }
    const NgEntityType type = (NgEntityType)w->type[i];
    const float rot_y = (type == NG_ENTITY_CUBE) ? cube_yaw : w->rot_y[i];
    if (type == NG_ENTITY_SPHERE) {
      mod_render_draw_entity_live(&ctx->sphere, type, w->pos_x[i], w->pos_y[i], w->pos_z[i], rot_y);
    } else if (type == NG_ENTITY_CUBE) {
      mod_render_draw_entity_live(&ctx->cube, type, w->pos_x[i], w->pos_y[i], w->pos_z[i], rot_y);
    }
  }
  EndMode3D();

  DrawText(TextFormat("scene: %s (embed)", w->scene_id), 10, 10, 20, RAYWHITE);
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
  Color bg = (Color){0, 0, 0, 255};
  if (strcmp(ctx->scene_label, "sphere") == 0) {
    bg = ctx->sphere.bg;
  } else if (strcmp(ctx->scene_label, "cube") == 0) {
    bg = ctx->cube.bg;
  }
  ClearBackground(bg);
  mod_render_update_camera(ctx, ctx->scene_label);

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
    if (e->type == NG_ENTITY_CUBE && mod_scene_client_fields_active()) {
      NgEntitySnap cube = *e;
      cube.rot_y = mod_scene_get_rot_y();
      mod_render_draw_entity(&ctx->cube, &cube, p, ctx->alpha);
    } else if (e->type == NG_ENTITY_SPHERE) {
      mod_render_draw_entity(&ctx->sphere, e, p, ctx->alpha);
    } else if (e->type == NG_ENTITY_CUBE) {
      mod_render_draw_entity(&ctx->cube, e, p, ctx->alpha);
    }
  }
  EndMode3D();

  DrawText(TextFormat("scene: %s (net)", ctx->scene_label), 10, 10, 20, RAYWHITE);
}

// agent: composer-2.5 | 2026-07-25 | apply action bounce state | b5c6d7
// agent: composer-2.5 | 2026-07-26 | session bootstrap render state | d8e9f0
void mod_render_apply_session(const NgSessionState *session) {
  ModRenderCtx *ctx = &g_render_ctx;
  if (!session) {
    return;
  }
  strncpy(ctx->scene_label, session->scene_id, sizeof(ctx->scene_label) - 1);
  ctx->scene_label[sizeof(ctx->scene_label) - 1] = '\0';
  ctx->have_session = true;
  ctx->client_fields = session->client_fields;
}

void mod_render_set_cube_rot_y(float rot_y) {
  if (mod_scene_client_fields_active()) {
    mod_input_set_pred_yaw(rot_y);
  }
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
    strncpy(ctx->scene_label, result->state.scene_id, sizeof(ctx->scene_label) - 1);
    ctx->scene_label[sizeof(ctx->scene_label) - 1] = '\0';
    for (int i = 0; i < ctx->curr.entity_count; i++) {
      if (ctx->curr.entities[i].type == NG_ENTITY_CUBE && !mod_scene_client_fields_active()) {
        mod_input_set_pred_yaw(ctx->curr.entities[i].rot_y);
        break;
      }
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
      strncpy(ctx->scene_label, msg->snapshot->scene_id, sizeof(ctx->scene_label) - 1);
      for (int i = 0; i < ctx->curr.entity_count; i++) {
        if (ctx->curr.entities[i].type == NG_ENTITY_CUBE && !mod_scene_client_fields_active()) {
          mod_input_set_pred_yaw(ctx->curr.entities[i].rot_y);
          break;
        }
      }
    }
    return true;
  case NG_MSG_EVENT:
    if (msg->text) {
      strncpy(ctx->scene_label, msg->text, sizeof(ctx->scene_label) - 1);
    }
    return true;
  case NG_MSG_TICK:
#if defined(NG_HAS_EMBEDDED)
    if (mod_net_is_embedded()) {
      mod_input_apply_pred(msg->dt);
      return true;
    }
#endif
    if (ctx->have_curr) {
      ctx->alpha += msg->dt * 20.0f;
      if (ctx->alpha > 1.0f) {
        ctx->alpha = 1.0f;
      }
    }
    mod_input_apply_pred(msg->dt);
    return true;
  case NG_MSG_DRAW:
#if defined(NG_HAS_EMBEDDED)
    if (mod_net_is_embedded()) {
      mod_render_draw_embedded(ctx);
      return true;
    }
#endif
    if (ctx->have_curr || ctx->have_session) {
      mod_render_draw_scene(ctx);
    } else {
      // agent: composer-2.5 | 2026-07-25 | embedded waiting UI text | 9236ea
      ClearBackground(BLACK);
      char line[160];
#if defined(NG_HAS_EMBEDDED)
      if (mod_net_is_embedded()) {
        const double elapsed = mod_net_connect_elapsed();
        if (elapsed > 8.0) {
          snprintf(line, sizeof(line), "embedded: no snapshot — restart client");
        } else {
          snprintf(line, sizeof(line), "embedded: waiting for snapshot...");
        }
      } else
#endif
      {
        char host[64];
        uint16_t port = 0;
        mod_net_endpoint(host, sizeof(host), &port);
        const double elapsed = mod_net_connect_elapsed();
        if (!mod_net_is_connected()) {
          if (elapsed > 8.0) {
            snprintf(line, sizeof(line), "no server at %s:%u — run ./build/ngame_server", host,
                     port);
          } else {
            snprintf(line, sizeof(line), "connecting to %s:%u...", host, port);
          }
        } else if (elapsed > 8.0) {
          snprintf(line, sizeof(line), "no snapshot from %s:%u — restart ngame_server", host,
                   port);
        } else {
          snprintf(line, sizeof(line), "waiting for snapshot from %s:%u...", host, port);
        }
      }
      DrawText(line, 20, 20, 18, GRAY);
#if defined(NG_HAS_EMBEDDED)
      if (!mod_net_is_embedded())
#endif
      {
        const double elapsed = mod_net_connect_elapsed();
        if (elapsed > 8.0) {
          DrawText("client keeps retrying; start server then leave this window open", 20, 44, 14,
                   DARKGRAY);
        }
      }
    }
    return true;
  default:
    return false;
  }
}

static bool mod_render_init(void *vctx) {
  ModRenderCtx *ctx = (ModRenderCtx *)vctx;
  memset(ctx, 0, sizeof(*ctx));
  strncpy(ctx->scene_label, "sphere", sizeof(ctx->scene_label) - 1);
  mod_render_init_assets(ctx);
  return true;
}

static void mod_render_shutdown(void *vctx) {
  ModRenderCtx *ctx = (ModRenderCtx *)vctx;
  mod_render_unload_asset(&ctx->sphere);
  mod_render_unload_asset(&ctx->cube);
}

static const NgModOps g_render_ops = {
    .name = "render",
    .dest = NG_BUS_RENDER,
    .init = mod_render_init,
    .shutdown = mod_render_shutdown,
    .on_msg = mod_render_on_msg,
};

const NgModOps *mod_render_ops(void) { return &g_render_ops; }

void *mod_render_ctx(void) { return &g_render_ctx; }

bool mod_render_has_snapshot(void) {
#if defined(NG_HAS_EMBEDDED)
  if (mod_net_is_embedded()) {
    return ng_embed_ready();
  }
#endif
  return g_render_ctx.have_curr || g_render_ctx.have_session;
}
