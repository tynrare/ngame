// agent: composer-2.5 | 2026-07-25 | cube scoped scene | 6e4a0c
#include "ng_path.h"
#include "ng_scene.h"
#include "ng_scene_cube.h"
#include "ng_shader.h"
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <stdlib.h>

typedef struct NgSceneCube {
  NgScene base;
  Model model;
  NgShader shader;
  Camera3D camera;
  float yaw;
  float time;
  bool ready;
} NgSceneCube;

static NgScene *cube_create(void) {
  NgSceneCube *s = (NgSceneCube *)calloc(1, sizeof(NgSceneCube));
  if (!s) {
    return NULL;
  }
  s->base.ops = ng_scene_cube_ops();
  return (NgScene *)s;
}

static void cube_destroy(NgScene *self) {
  NgSceneCube *s = (NgSceneCube *)self;
  if (!s) {
    return;
  }
  if (s->ready) {
    ng_shader_unload(&s->shader);
    UnloadModel(s->model);
  }
  free(s);
}

static void cube_enter(NgScene *self) {
  NgSceneCube *s = (NgSceneCube *)self;
  if (s->ready) {
    return;
  }

  Mesh mesh = GenMeshCube(1.5f, 1.5f, 1.5f);
  s->model = LoadModelFromMesh(mesh);
  s->shader = ng_shader_load(NG_RES_ROOT "shaders/mesh.vs",
                             NG_RES_ROOT "shaders/cube.fs");
  s->model.materials[0].shader = s->shader.handle;

  s->camera = (Camera3D){
      .position = (Vector3){4.0f, 3.0f, 4.0f},
      .target = (Vector3){0.0f, 0.0f, 0.0f},
      .up = (Vector3){0.0f, 1.0f, 0.0f},
      .fovy = 45.0f,
      .projection = CAMERA_PERSPECTIVE,
  };
  s->yaw = 0.0f;
  s->time = 0.0f;
  s->ready = true;
}

static void cube_exit(NgScene *self) {
  (void)self;
}

static void cube_update(NgScene *self, float dt) {
  NgSceneCube *s = (NgSceneCube *)self;
  if (!s->ready) {
    return;
  }
  s->time += dt;
  if (IsKeyDown(KEY_A)) {
    s->yaw -= 1.5f * dt;
  }
  if (IsKeyDown(KEY_D)) {
    s->yaw += 1.5f * dt;
  }

  const float dist = 6.0f;
  s->camera.position.x = sinf(s->yaw) * dist;
  s->camera.position.z = cosf(s->yaw) * dist;
  s->camera.position.y = 2.5f;
}

static void cube_draw(NgScene *self) {
  NgSceneCube *s = (NgSceneCube *)self;
  if (!s->ready) {
    return;
  }

  ClearBackground((Color){48, 24, 8, 255});
  ng_shader_set_common(&s->shader, s->time);
  if (s->shader.loc_tint >= 0) {
    const float tint[3] = {1.0f, 0.55f, 0.2f};
    SetShaderValue(s->shader.handle, s->shader.loc_tint, tint,
                   SHADER_UNIFORM_VEC3);
  }

  BeginMode3D(s->camera);
  DrawModelEx(s->model, Vector3Zero(), (Vector3){0.0f, 1.0f, 0.0f}, s->yaw * 57.2958f,
              (Vector3){1.0f, 1.0f, 1.0f}, WHITE);
  EndMode3D();

  DrawText("scene: cube  (A/D yaw)", 10, 10, 20, RAYWHITE);
}

static const NgSceneOps g_cube_ops = {
    .id = "cube",
    .create = cube_create,
    .destroy = cube_destroy,
    .enter = cube_enter,
    .exit = cube_exit,
    .update = cube_update,
    .draw = cube_draw,
};

const NgSceneOps *ng_scene_cube_ops(void) { return &g_cube_ops; }
