// agent: composer-2.5 | 2026-07-29 | lockstep sim mode physics | 9871a5
// agent: composer-2.5 | 2026-07-30 | physics export import names | 837963
#ifndef MOD_SCENE_PHYSICS_H
#define MOD_SCENE_PHYSICS_H

#include "engine/ng_sync.h"
#include <stdbool.h>
#include <stdint.h>

#define NG_SCENE_PHYSICS_DESC_MAX 32

typedef enum NgScenePhysShapeType {
  NG_PHYS_SHAPE_BOX = 0,
} NgScenePhysShapeType;

typedef enum NgScenePhysBodyType {
  NG_PHYS_BODY_STATIC = 0,
  NG_PHYS_BODY_KINEMATIC = 1,
  NG_PHYS_BODY_DYNAMIC = 2,
} NgScenePhysBodyType;

typedef enum NgPhysSimMode {
  NG_PHYS_SIM_SERVER = 0,
  NG_PHYS_SIM_LOCKSTEP = 1,
} NgPhysSimMode;

typedef struct NgScenePhysShapeDesc {
  bool alive;
  char name[32];
  NgScenePhysShapeType type;
  float hx;
  float hy;
  float hz;
  float density;
  float friction;
} NgScenePhysShapeDesc;

typedef struct NgScenePhysBodyDesc {
  bool alive;
  char name[32];
  NgScenePhysBodyType type;
  char shape[32];
} NgScenePhysBodyDesc;

typedef struct ModScenePhysicsCtx {
  bool world_alive;
  uint32_t world_bits;
  NgPhysSimMode sim_mode;
  NgScenePhysShapeDesc shapes[NG_SCENE_PHYSICS_DESC_MAX];
  int shape_count;
  NgScenePhysBodyDesc bodies[NG_SCENE_PHYSICS_DESC_MAX];
  int body_count;
} ModScenePhysicsCtx;

void mod_scene_physics_reset(void);
void mod_scene_physics_set_sim_mode(NgPhysSimMode mode);
NgPhysSimMode mod_scene_physics_sim_mode(void);
bool mod_scene_physics_is_lockstep(void);

bool mod_scene_physics_describe_shape(const char *name, const char *type, float hx, float hy,
                                      float hz, float density, float friction);
bool mod_scene_physics_describe_body(const char *name, const char *type, const char *shape);
bool mod_scene_physics_dispose(const char *kind, const char *name);

bool mod_scene_physics_should_simulate(NgSyncMode sync, bool on_server, bool is_controller);
bool mod_scene_physics_attach(int handle, const char *body_name, NgSyncMode sync, bool on_server,
                              bool is_controller, const float pos[3], const float rot[3]);
void mod_scene_physics_detach(int handle);
void mod_scene_physics_destroy_world(void);

void mod_scene_physics_fixed_step(float fixed_dt, bool on_server, bool is_controller);
uint32_t mod_scene_physics_checksum(void);

/* Box3D world save/restore for lockstep late-join. Caller frees *out with b3FreeSaveData. */
bool mod_scene_physics_export(uint8_t **out, int *out_size);
bool mod_scene_physics_import(const uint8_t *data, int size);

#endif
// agent: composer-2.5 | 2026-07-29 | lockstep sim mode physics | 9871a5
// agent: composer-2.5 | 2026-07-30 | physics export import names | 837963
