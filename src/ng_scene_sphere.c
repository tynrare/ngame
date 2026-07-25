// agent: composer-2.5 | 2026-07-25 | sphere scoped scene | 9b2d1f
#include "ng_path.h"
#include "ng_scene.h"
#include "ng_scene_sphere.h"
#include "ng_shader.h"
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <stdlib.h>

typedef struct NgSceneSphere {
  NgScene base;
  Model model;
  NgShader shader;
  Camera3D camera;
  float phase;
  bool ready;
} NgSceneSphere;

static NgScene *sphere_create(void) {
  NgSceneSphere *s = (NgSceneSphere *)calloc(1, sizeof(NgSceneSphere));
  if (!s) {
    return NULL;
  }
  s->base.ops = ng_scene_sphere_ops();
  return (NgScene *)s;
}

static void sphere_destroy(NgScene *self) {
  NgSceneSphere *s = (NgSceneSphere *)self;
  if (!s) {
    return;
  }
  if (s->ready) {
    ng_shader_unload(&s->shader);
    UnloadModel(s->model);
  }
  free(s);
}

static void sphere_enter(NgScene *self) {
  NgSceneSphere *s = (NgSceneSphere *)self;
  if (s->ready) {
    return;
  }

  Mesh mesh = GenMeshSphere(1.0f, 32, 32);
  s->model = LoadModelFromMesh(mesh);
  s->shader = ng_shader_load(NG_RES_ROOT "shaders/mesh.vs",
                             NG_RES_ROOT "shaders/sphere.fs");
  s->model.materials[0].shader = s->shader.handle;

  s->camera = (Camera3D){
      .position = (Vector3){3.0f, 2.0f, 5.0f},
      .target = (Vector3){0.0f, 0.0f, 0.0f},
      .up = (Vector3){0.0f, 1.0f, 0.0f},
      .fovy = 45.0f,
      .projection = CAMERA_PERSPECTIVE,
  };
  s->phase = 0.0f;
  s->ready = true;
}

static void sphere_exit(NgScene *self) {
  (void)self;
}

static void sphere_update(NgScene *self, float dt) {
  NgSceneSphere *s = (NgSceneSphere *)self;
  if (!s->ready) {
    return;
  }
  s->phase += dt;
  const float t = s->phase;
  const float radius = 6.0f;
  s->camera.position.x = sinf(t * 0.6f) * radius;
  s->camera.position.z = cosf(t * 0.6f) * radius;
  s->camera.position.y = 2.0f + sinf(t * 0.3f) * 0.5f;
}

static void sphere_draw(NgScene *self) {
  NgSceneSphere *s = (NgSceneSphere *)self;
  if (!s->ready) {
    return;
  }

  ClearBackground((Color){12, 20, 48, 255});
  ng_shader_set_common(&s->shader, s->phase);
  if (s->shader.loc_tint >= 0) {
    const float tint[3] = {0.35f, 0.55f, 1.0f};
    SetShaderValue(s->shader.handle, s->shader.loc_tint, tint,
                   SHADER_UNIFORM_VEC3);
  }

  BeginMode3D(s->camera);
  DrawModel(s->model, Vector3Zero(), 1.0f, WHITE);
  EndMode3D();

  DrawText("scene: sphere", 10, 10, 20, RAYWHITE);
}

static const NgSceneOps g_sphere_ops = {
    .id = "sphere",
    .create = sphere_create,
    .destroy = sphere_destroy,
    .enter = sphere_enter,
    .exit = sphere_exit,
    .update = sphere_update,
    .draw = sphere_draw,
};

const NgSceneOps *ng_scene_sphere_ops(void) { return &g_sphere_ops; }
