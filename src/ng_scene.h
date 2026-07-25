// agent: composer-2.5 | 2026-07-25 | scoped scene registry | 5a3c8e
#ifndef NG_SCENE_H
#define NG_SCENE_H

#include <stdbool.h>

typedef struct NgScene NgScene;

typedef struct NgSceneOps {
  const char *id;
  NgScene *(*create)(void);
  void (*destroy)(NgScene *self);
  void (*enter)(NgScene *self);
  void (*exit)(NgScene *self);
  void (*update)(NgScene *self, float dt);
  void (*draw)(NgScene *self);
} NgSceneOps;

struct NgScene {
  const NgSceneOps *ops;
};

bool ng_scene_register(const NgSceneOps *ops);
bool ng_scene_load(const char *id);
void ng_scene_update(float dt);
void ng_scene_draw(void);
void ng_scene_shutdown(void);
const char *ng_scene_active_id(void);

const NgSceneOps *ng_scene_sphere_ops(void);
const NgSceneOps *ng_scene_cube_ops(void);

#endif
