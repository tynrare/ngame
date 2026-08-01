// agent: composer-2.5 | 2026-07-29 | lockstep sim mode physics | 9871a5
// agent: composer-2.5 | 2026-07-30 | physics export import names | 837963
// agent: composer-2.5 | 2026-07-30 | physics gravity sensor api | 8ec243
#ifndef MOD_SCENE_PHYSICS_H
#define MOD_SCENE_PHYSICS_H

#include "engine/ng_sync.h"
#include <stdbool.h>
#include <stdint.h>

#define NG_SCENE_PHYSICS_DESC_MAX 32

typedef enum NgScenePhysShapeType {
  NG_PHYS_SHAPE_BOX = 0,
  // agent: composer-2.5 | 2026-07-30 | sphere shape enum | f52c4b
  NG_PHYS_SHAPE_SPHERE = 1,
} NgScenePhysShapeType;

typedef enum NgScenePhysBodyType {
  NG_PHYS_BODY_STATIC = 0,
  NG_PHYS_BODY_KINEMATIC = 1,
  NG_PHYS_BODY_DYNAMIC = 2,
} NgScenePhysBodyType;

// agent: composer-2.5 | 2026-08-01 | hybrid sim mode enum | 772934
typedef enum NgPhysSimMode {
  NG_PHYS_SIM_SERVER = 0,
  NG_PHYS_SIM_LOCKSTEP = 1, /* classic Gaffer wait-for-all */
  NG_PHYS_SIM_HYBRID = 2,   /* confirm + predict + adapt + ghost */
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
  // agent: composer-2.5 | 2026-07-30 | physics gravity sensor api | 8ec243
  bool sensor;
  // agent: composer-2.5 | 2026-07-30 | sphere shape enum | f52c4b
  float radius; /* sphere only; hx unused for mass when type=sphere */
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
  // agent: composer-2.5 | 2026-07-30 | physics gravity sensor api | 8ec243
  float gravity[3];
  bool gravity_set;
  NgScenePhysShapeDesc shapes[NG_SCENE_PHYSICS_DESC_MAX];
  int shape_count;
  NgScenePhysBodyDesc bodies[NG_SCENE_PHYSICS_DESC_MAX];
  int body_count;
} ModScenePhysicsCtx;

void mod_scene_physics_reset(void);
void mod_scene_physics_set_sim_mode(NgPhysSimMode mode);
NgPhysSimMode mod_scene_physics_sim_mode(void);
bool mod_scene_physics_is_lockstep(void); /* pure Gaffer only */
// agent: composer-2.5 | 2026-08-01 | hybrid sim mode enum | 772934
bool mod_scene_physics_is_hybrid(void);
bool mod_scene_physics_is_input_sim(void); /* lockstep or hybrid */
// agent: composer-2.5 | 2026-07-30 | physics gravity sensor api | 8ec243
void mod_scene_physics_set_gravity(float gx, float gy, float gz);

bool mod_scene_physics_describe_shape(const char *name, const char *type, float hx, float hy,
                                      float hz, float density, float friction, bool sensor);
bool mod_scene_physics_describe_body(const char *name, const char *type, const char *shape);
bool mod_scene_physics_dispose(const char *kind, const char *name);

bool mod_scene_physics_should_simulate(NgSyncMode sync, bool on_server, bool is_controller);
bool mod_scene_physics_attach(int handle, const char *body_name, NgSyncMode sync, bool on_server,
                              bool is_controller, const float pos[3], const float rot[3]);
void mod_scene_physics_detach(int handle);
void mod_scene_physics_drive_proxy(int handle, const float pos[3], const float rot[3],
                                   const float lin_vel[3], const float ang_vel[3]);
// agent: composer-2.5 | 2026-07-30 | apply linear impulse helper | 4c0fcc
bool mod_scene_physics_apply_impulse(int handle, float ix, float iy, float iz);
// agent: composer-2.5 | 2026-07-30 | apply force and torque helpers | 257985
bool mod_scene_physics_apply_force(int handle, float fx, float fy, float fz);
bool mod_scene_physics_apply_torque(int handle, float tx, float ty, float tz);
// agent: composer-2.5 | 2026-07-30 | physics gravity sensor api | 8ec243
bool mod_scene_physics_set_linear_velocity(int handle, float vx, float vy, float vz);
bool mod_scene_physics_get_linear_velocity(int handle, float out[3]);
float mod_scene_physics_get_mass(int handle);
void mod_scene_physics_destroy_world(void);

void mod_scene_physics_fixed_step(float fixed_dt, bool on_server, bool is_controller);
uint32_t mod_scene_physics_checksum(void);

/* Box3D world save/restore for lockstep late-join. Caller frees *out with b3FreeSaveData. */
bool mod_scene_physics_export(uint8_t **out, int *out_size);
bool mod_scene_physics_import(const uint8_t *data, int size);

/* Local rollback ring (b3World_Save — not recording/replay). */
// agent: composer-2.5 | 2026-07-31 | save ring for rollback | 2fa13d
// agent: composer-2.5 | 2026-07-31 | drop mode b comment phys | 127036
void mod_scene_physics_save_ring_clear(void);
void mod_scene_physics_save_ring_push(uint32_t tick);
bool mod_scene_physics_save_ring_restore(uint32_t tick);

#endif
// agent: composer-2.5 | 2026-07-29 | lockstep sim mode physics | 9871a5
// agent: composer-2.5 | 2026-07-30 | physics export import names | 837963
// agent: composer-2.5 | 2026-07-30 | physics proxy drive API | 36fd45
// agent: composer-2.5 | 2026-07-30 | apply linear impulse helper | 4c0fcc
// agent: composer-2.5 | 2026-07-30 | apply force and torque helpers | 257985
// agent: composer-2.5 | 2026-07-30 | physics gravity sensor api | 8ec243
// agent: composer-2.5 | 2026-07-30 | sphere shape enum | f52c4b
// agent: composer-2.5 | 2026-07-31 | save ring decls | e478f4
// agent: composer-2.5 | 2026-07-31 | save ring for rollback | 2fa13d
// agent: composer-2.5 | 2026-07-31 | drop mode b comment phys | 127036
// agent: composer-2.5 | 2026-08-01 | hybrid sim mode enum | 772934
