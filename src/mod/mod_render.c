// agent: composer-2.5 | 2026-07-25 | client render module | g0j28e
#include "mod_render.h"
#include "core/ng_bus.h"
#include "mod/mod_net.h"
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
  Camera3D camera;
  float pred_yaw;
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

static void mod_render_update_camera(ModRenderCtx *ctx) {
  float phase = 0.0f;
  float yaw = ctx->pred_yaw;
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
    phase = mod_render_lerp(p->phase, e->phase, ctx->alpha);
    if (e->type == NG_ENTITY_CUBE) {
      yaw = mod_render_lerp(p->rot_y, e->rot_y, ctx->alpha);
      ctx->pred_yaw = yaw;
    }
  }

  if (strcmp(ctx->scene_label, "sphere") == 0) {
    const float radius = 6.0f;
    ctx->camera.position.x = sinf(phase * 0.6f) * radius;
    ctx->camera.position.z = cosf(phase * 0.6f) * radius;
    ctx->camera.position.y = 2.0f + sinf(phase * 0.3f) * 0.5f;
  } else if (strcmp(ctx->scene_label, "cube") == 0) {
    const float dist = 6.0f;
    ctx->camera.position.x = sinf(yaw) * dist;
    ctx->camera.position.z = cosf(yaw) * dist;
    ctx->camera.position.y = 2.5f;
  }
}

static void mod_render_draw_entity(const RenderAsset *a, const NgEntitySnap *e,
                                   const NgEntitySnap *p, float alpha) {
  if (!a->ready) {
    return;
  }
  const float phase = mod_render_lerp(p ? p->phase : e->phase, e->phase, alpha);
  const float yaw = mod_render_lerp(p ? p->rot_y : e->rot_y, e->rot_y, alpha);

  ng_shader_set_common((NgShader *)&a->shader, phase);
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
  mod_render_update_camera(ctx);

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
    if (e->type == NG_ENTITY_SPHERE) {
      mod_render_draw_entity(&ctx->sphere, e, p, ctx->alpha);
    } else if (e->type == NG_ENTITY_CUBE) {
      mod_render_draw_entity(&ctx->cube, e, p, ctx->alpha);
    }
  }
  EndMode3D();

  DrawText(TextFormat("scene: %s (net)", ctx->scene_label), 10, 10, 20, RAYWHITE);
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
        if (ctx->curr.entities[i].type == NG_ENTITY_CUBE) {
          ctx->pred_yaw = ctx->curr.entities[i].rot_y;
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
    if (ctx->have_curr) {
      ctx->alpha += msg->dt * 20.0f;
      if (ctx->alpha > 1.0f) {
        ctx->alpha = 1.0f;
      }
    }
    if (IsKeyDown(KEY_A)) {
      ctx->pred_yaw -= 1.5f * msg->dt;
    }
    if (IsKeyDown(KEY_D)) {
      ctx->pred_yaw += 1.5f * msg->dt;
    }
    return true;
  case NG_MSG_DRAW:
    if (ctx->have_curr) {
      mod_render_draw_scene(ctx);
    } else {
      // agent: composer-2.5 | 2026-07-25 | connect timeout hint UI | b2e81a
      ClearBackground(BLACK);
      char host[64];
      uint16_t port = 0;
      mod_net_endpoint(host, sizeof(host), &port);
      char line[160];
      const double elapsed = mod_net_connect_elapsed();
      if (!mod_net_is_connected()) {
        if (elapsed > 8.0) {
          snprintf(line, sizeof(line), "no server at %s:%u — run ./build/ngame_server", host,
                   port);
        } else {
          snprintf(line, sizeof(line), "connecting to %s:%u...", host, port);
        }
      } else if (elapsed > 8.0) {
        // agent: composer-2.5 | 2026-07-25 | snapshot timeout hint | 44214c
        snprintf(line, sizeof(line), "no snapshot from %s:%u — restart ngame_server", host, port);
      } else {
        snprintf(line, sizeof(line), "waiting for snapshot from %s:%u...", host, port);
      }
      DrawText(line, 20, 20, 18, GRAY);
      if (elapsed > 8.0) {
        DrawText("client keeps retrying; start server then leave this window open", 20, 44, 14,
                 DARKGRAY);
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

bool mod_render_has_snapshot(void) { return g_render_ctx.have_curr; }
