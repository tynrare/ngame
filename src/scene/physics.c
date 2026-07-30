// agent: composer-2.5 | 2026-07-29 | lockstep sim mode physics | f77a9c
// agent: composer-2.5 | 2026-07-30 | physics export import names | 1b75f3
#include "physics.h"
#include "engine/ng_log.h"
#include "engine/ng_mod.h"
#include "graph.h"
#include "lockstep.h"
#include "scene/runtime.h"
#include "world/ng_world.h"
#include "box3d/box3d.h"
#include "box3d/collision.h"
#include "box3d/id.h"
#include "box3d/math_functions.h"
#include <math.h>
#include <string.h>

#define GPHYS() (*mod_scene_runtime_physics())

static b3WorldId mod_scene_physics_world_id(void) {
  return b3LoadWorldId(GPHYS().world_bits);
}

static void mod_scene_physics_store_world(b3WorldId id) {
  GPHYS().world_bits = b3StoreWorldId(id);
  GPHYS().world_alive = b3World_IsValid(id);
}

static NgScenePhysShapeDesc *mod_scene_physics_find_shape(const char *name) {
  if (!name) {
    return NULL;
  }
  for (int i = 0; i < GPHYS().shape_count; i++) {
    NgScenePhysShapeDesc *s = &GPHYS().shapes[i];
    if (s->alive && strcmp(s->name, name) == 0) {
      return s;
    }
  }
  return NULL;
}

static NgScenePhysBodyDesc *mod_scene_physics_find_body(const char *name) {
  if (!name) {
    return NULL;
  }
  for (int i = 0; i < GPHYS().body_count; i++) {
    NgScenePhysBodyDesc *b = &GPHYS().bodies[i];
    if (b->alive && strcmp(b->name, name) == 0) {
      return b;
    }
  }
  return NULL;
}

void mod_scene_physics_reset(void) {
  mod_scene_physics_destroy_world();
  memset(mod_scene_runtime_physics(), 0, sizeof(ModScenePhysicsCtx));
}

void mod_scene_physics_set_sim_mode(NgPhysSimMode mode) {
  GPHYS().sim_mode = mode;
}

NgPhysSimMode mod_scene_physics_sim_mode(void) {
  return GPHYS().sim_mode;
}

bool mod_scene_physics_is_lockstep(void) {
  return GPHYS().sim_mode == NG_PHYS_SIM_LOCKSTEP;
}

bool mod_scene_physics_describe_shape(const char *name, const char *type, float hx, float hy,
                                      float hz, float density, float friction) {
  if (!name || name[0] == '\0') {
    return false;
  }
  if (type && strcmp(type, "box") != 0 && type[0] != '\0') {
    return false;
  }
  NgScenePhysShapeDesc *existing = mod_scene_physics_find_shape(name);
  if (!existing) {
    if (GPHYS().shape_count >= NG_SCENE_PHYSICS_DESC_MAX) {
      return false;
    }
    existing = &GPHYS().shapes[GPHYS().shape_count++];
    memset(existing, 0, sizeof(*existing));
    strncpy(existing->name, name, sizeof(existing->name) - 1);
  }
  existing->alive = true;
  existing->type = NG_PHYS_SHAPE_BOX;
  existing->hx = hx > 0.0f ? hx : 0.5f;
  existing->hy = hy > 0.0f ? hy : 0.5f;
  existing->hz = hz > 0.0f ? hz : 0.5f;
  existing->density = density >= 0.0f ? density : 1.0f;
  existing->friction = friction >= 0.0f ? friction : 0.3f;
  return true;
}

bool mod_scene_physics_describe_body(const char *name, const char *type, const char *shape) {
  if (!name || !shape || name[0] == '\0' || shape[0] == '\0') {
    return false;
  }
  NgScenePhysBodyType bt = NG_PHYS_BODY_STATIC;
  if (type) {
    if (strcmp(type, "dynamic") == 0) {
      bt = NG_PHYS_BODY_DYNAMIC;
    } else if (strcmp(type, "kinematic") == 0) {
      bt = NG_PHYS_BODY_KINEMATIC;
    } else {
      bt = NG_PHYS_BODY_STATIC;
    }
  }
  NgScenePhysBodyDesc *existing = mod_scene_physics_find_body(name);
  if (!existing) {
    if (GPHYS().body_count >= NG_SCENE_PHYSICS_DESC_MAX) {
      return false;
    }
    existing = &GPHYS().bodies[GPHYS().body_count++];
    memset(existing, 0, sizeof(*existing));
    strncpy(existing->name, name, sizeof(existing->name) - 1);
  }
  existing->alive = true;
  existing->type = bt;
  strncpy(existing->shape, shape, sizeof(existing->shape) - 1);
  return true;
}

bool mod_scene_physics_dispose(const char *kind, const char *name) {
  if (!kind || !name) {
    return false;
  }
  if (strcmp(kind, "shape") == 0) {
    NgScenePhysShapeDesc *s = mod_scene_physics_find_shape(name);
    if (s) {
      s->alive = false;
      return true;
    }
  } else if (strcmp(kind, "body") == 0) {
    NgScenePhysBodyDesc *b = mod_scene_physics_find_body(name);
    if (b) {
      b->alive = false;
      return true;
    }
  }
  return false;
}

bool mod_scene_physics_should_simulate(NgSyncMode sync, bool on_server, bool is_controller) {
  if (mod_scene_physics_is_lockstep()) {
    (void)sync;
    (void)on_server;
    (void)is_controller;
    return true;
  }
  if (sync == NG_SYNC_SERVER) {
    return on_server;
  }
  if (sync == NG_SYNC_SHARED || sync == NG_SYNC_LOCAL) {
    return !on_server;
  }
  if (sync == NG_SYNC_OWNER) {
    return !on_server && is_controller;
  }
  return false;
}

static bool mod_scene_physics_ensure_world(void) {
  if (GPHYS().world_alive && b3World_IsValid(mod_scene_physics_world_id())) {
    return true;
  }
  b3WorldDef def = b3DefaultWorldDef();
  def.gravity = (b3Vec3){0.0f, -10.0f, 0.0f};
  def.workerCount = 1;
  b3WorldId id = b3CreateWorld(&def);
  if (!b3World_IsValid(id)) {
    return false;
  }
  mod_scene_physics_store_world(id);
  return true;
}

void mod_scene_physics_destroy_world(void) {
  if (GPHYS().world_alive) {
    b3WorldId id = mod_scene_physics_world_id();
    if (b3World_IsValid(id)) {
      b3DestroyWorld(id);
    }
  }
  GPHYS().world_alive = false;
  GPHYS().world_bits = 0;
}

static b3Quat mod_scene_physics_quat_from_euler(const float rot[3]) {
  b3Quat qx = b3MakeQuatFromAxisAngle((b3Vec3){1.0f, 0.0f, 0.0f}, rot ? rot[0] : 0.0f);
  b3Quat qy = b3MakeQuatFromAxisAngle((b3Vec3){0.0f, 1.0f, 0.0f}, rot ? rot[1] : 0.0f);
  b3Quat qz = b3MakeQuatFromAxisAngle((b3Vec3){0.0f, 0.0f, 1.0f}, rot ? rot[2] : 0.0f);
  return b3MulQuat(qz, b3MulQuat(qy, qx));
}

static void mod_scene_physics_euler_from_quat(b3Quat q, float rot[3]) {
  const float x = q.v.x;
  const float y = q.v.y;
  const float z = q.v.z;
  const float w = q.s;
  const float sinr_cosp = 2.0f * (w * x + y * z);
  const float cosr_cosp = 1.0f - 2.0f * (x * x + y * y);
  rot[0] = atan2f(sinr_cosp, cosr_cosp);
  float sinp = 2.0f * (w * y - z * x);
  if (sinp > 1.0f) {
    sinp = 1.0f;
  }
  if (sinp < -1.0f) {
    sinp = -1.0f;
  }
  rot[1] = asinf(sinp);
  const float siny_cosp = 2.0f * (w * z + x * y);
  const float cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
  rot[2] = atan2f(siny_cosp, cosy_cosp);
}

bool mod_scene_physics_attach(int handle, const char *body_name, NgSyncMode sync, bool on_server,
                              bool is_controller, const float pos[3], const float rot[3]) {
  // agent: composer-2.5 | 2026-07-30 | kinematic proxy attach drive | a64b5e
  // agent: composer-2.5 | 2026-07-30 | skip view attach under lockstep | dfc161
  NgSceneInst *inst = mod_scene_graph_inst_by_handle(handle);
  if (!inst || !body_name || body_name[0] == '\0') {
    return false;
  }
  strncpy(inst->body, body_name, sizeof(inst->body) - 1);
  inst->phys_proxy = false;
  /* One Box3D world per process under lockstep: view skips attach when server slot owns sim. */
  if (mod_scene_physics_is_lockstep() && mod_scene_runtime_active() == &g_scene_view &&
      g_scene_server.scene.loaded && g_scene_server.physics.sim_mode == NG_PHYS_SIM_LOCKSTEP) {
    inst->body_id_bits = 0;
    return true;
  }
  const bool sim = mod_scene_physics_should_simulate(sync, on_server, is_controller);
  /* View-side kinematic proxy for server-auth bodies (sim:server, not lockstep). */
  const bool want_proxy = !sim && !mod_scene_physics_is_lockstep() && sync == NG_SYNC_SERVER &&
                          !on_server;
  if (!sim && !want_proxy) {
    inst->body_id_bits = 0;
    return true;
  }
  /* Late-join: hold body create until Box3D snap restores the world. */
  if (mod_lockstep_awaiting_phys()) {
    NG_LOG_INFO("lockstep: defer body create key=%s (awaiting phys)", inst->key);
    inst->body_id_bits = 0;
    return true;
  }
  NgScenePhysBodyDesc *bdesc = mod_scene_physics_find_body(body_name);
  NgScenePhysShapeDesc *sdesc = bdesc ? mod_scene_physics_find_shape(bdesc->shape) : NULL;
  if (!bdesc || !sdesc) {
    return false;
  }
  if (!mod_scene_physics_ensure_world()) {
    return false;
  }
  b3BodyDef bodyDef = b3DefaultBodyDef();
  if (want_proxy) {
    bodyDef.type = b3_kinematicBody;
    inst->phys_proxy = true;
  } else if (bdesc->type == NG_PHYS_BODY_DYNAMIC) {
    bodyDef.type = b3_dynamicBody;
  } else if (bdesc->type == NG_PHYS_BODY_KINEMATIC) {
    bodyDef.type = b3_kinematicBody;
  } else {
    bodyDef.type = b3_staticBody;
  }
  bodyDef.position = (b3Pos){pos ? pos[0] : 0.0f, pos ? pos[1] : 0.0f, pos ? pos[2] : 0.0f};
  bodyDef.rotation = mod_scene_physics_quat_from_euler(rot);
  b3BodyId bodyId = b3CreateBody(mod_scene_physics_world_id(), &bodyDef);
  if (!b3Body_IsValid(bodyId)) {
    return false;
  }
  b3BoxHull box = b3MakeBoxHull(sdesc->hx, sdesc->hy, sdesc->hz);
  b3ShapeDef shapeDef = b3DefaultShapeDef();
  shapeDef.density = want_proxy ? 0.0f : sdesc->density;
  shapeDef.baseMaterial.friction = sdesc->friction;
  b3CreateHullShape(bodyId, &shapeDef, &box.base);
  if (inst->key[0] != '\0') {
    b3Body_SetName(bodyId, inst->key);
  }
  inst->body_id_bits = b3StoreBodyId(bodyId);
  return true;
}

void mod_scene_physics_detach(int handle) {
  NgSceneInst *inst = mod_scene_graph_inst_by_handle(handle);
  if (!inst || inst->body_id_bits == 0) {
    return;
  }
  b3BodyId id = b3LoadBodyId(inst->body_id_bits);
  if (b3Body_IsValid(id)) {
    b3DestroyBody(id);
  }
  inst->body_id_bits = 0;
  inst->phys_proxy = false;
}

void mod_scene_physics_drive_proxy(int handle, const float pos[3], const float rot[3],
                                   const float lin_vel[3], const float ang_vel[3]) {
  // agent: composer-2.5 | 2026-07-30 | kinematic proxy attach drive | a64b5e
  NgSceneInst *inst = mod_scene_graph_inst_by_handle(handle);
  if (!inst || !inst->phys_proxy || inst->body_id_bits == 0) {
    return;
  }
  b3BodyId id = b3LoadBodyId(inst->body_id_bits);
  if (!b3Body_IsValid(id)) {
    return;
  }
  if (pos && rot) {
    b3Body_SetTransform(id, (b3Pos){pos[0], pos[1], pos[2]},
                        mod_scene_physics_quat_from_euler(rot));
  }
  if (lin_vel) {
    b3Body_SetLinearVelocity(id, (b3Vec3){lin_vel[0], lin_vel[1], lin_vel[2]});
  }
  if (ang_vel) {
    b3Body_SetAngularVelocity(id, (b3Vec3){ang_vel[0], ang_vel[1], ang_vel[2]});
  }
}

// agent: composer-2.5 | 2026-07-30 | apply linear impulse helper | 3ee627
bool mod_scene_physics_apply_impulse(int handle, float ix, float iy, float iz) {
  NgSceneInst *inst = mod_scene_graph_inst_by_handle(handle);
  if (!inst || inst->body_id_bits == 0 || inst->phys_proxy) {
    return false;
  }
  b3BodyId id = b3LoadBodyId(inst->body_id_bits);
  if (!b3Body_IsValid(id)) {
    return false;
  }
  b3Body_ApplyLinearImpulseToCenter(id, (b3Vec3){ix, iy, iz}, true);
  return true;
}

// agent: composer-2.5 | 2026-07-30 | apply force and torque helpers | fc5a41
bool mod_scene_physics_apply_force(int handle, float fx, float fy, float fz) {
  NgSceneInst *inst = mod_scene_graph_inst_by_handle(handle);
  if (!inst || inst->body_id_bits == 0 || inst->phys_proxy) {
    return false;
  }
  b3BodyId id = b3LoadBodyId(inst->body_id_bits);
  if (!b3Body_IsValid(id)) {
    return false;
  }
  b3Body_ApplyForceToCenter(id, (b3Vec3){fx, fy, fz}, true);
  return true;
}

bool mod_scene_physics_apply_torque(int handle, float tx, float ty, float tz) {
  NgSceneInst *inst = mod_scene_graph_inst_by_handle(handle);
  if (!inst || inst->body_id_bits == 0 || inst->phys_proxy) {
    return false;
  }
  b3BodyId id = b3LoadBodyId(inst->body_id_bits);
  if (!b3Body_IsValid(id)) {
    return false;
  }
  b3Body_ApplyTorque(id, (b3Vec3){tx, ty, tz}, true);
  return true;
}

uint32_t mod_scene_physics_checksum(void) {
  uint32_t h = 2166136261u;
  const int n = mod_scene_graph_inst_count();
  const NgSceneInst *ordered[NG_SCENE_INST_MAX];
  int count = 0;
  for (int i = 0; i < n && count < NG_SCENE_INST_MAX; i++) {
    const NgSceneInst *inst = mod_scene_graph_inst_at(i);
    if (!inst || inst->body_id_bits == 0) {
      continue;
    }
    ordered[count++] = inst;
  }
  /* Stable order by entity key for cross-peer checksum. */
  for (int i = 0; i < count; i++) {
    for (int j = i + 1; j < count; j++) {
      if (strcmp(ordered[j]->key, ordered[i]->key) < 0) {
        const NgSceneInst *tmp = ordered[i];
        ordered[i] = ordered[j];
        ordered[j] = tmp;
      }
    }
  }
  for (int i = 0; i < count; i++) {
    const NgSceneInst *inst = ordered[i];
    b3BodyId id = b3LoadBodyId(inst->body_id_bits);
    if (!b3Body_IsValid(id)) {
      continue;
    }
    b3Pos p = b3Body_GetPosition(id);
    b3Quat q = b3Body_GetRotation(id);
    const float bits[7] = {(float)p.x, (float)p.y, (float)p.z, q.v.x, q.v.y, q.v.z, q.s};
    for (int k = 0; k < 7; k++) {
      uint32_t u;
      memcpy(&u, &bits[k], sizeof(u));
      h ^= u;
      h *= 16777619u;
    }
    for (const char *c = inst->key; *c; c++) {
      h ^= (uint8_t)*c;
      h *= 16777619u;
    }
  }
  return h;
}

void mod_scene_physics_fixed_step(float fixed_dt, bool on_server, bool is_controller) {
  if (!GPHYS().world_alive || !b3World_IsValid(mod_scene_physics_world_id())) {
    return;
  }
  bool any = false;
  const int n = mod_scene_graph_inst_count();
  for (int i = 0; i < n; i++) {
    const NgSceneInst *inst = mod_scene_graph_inst_at(i);
    if (!inst || inst->body_id_bits == 0) {
      continue;
    }
    if (mod_scene_physics_should_simulate(inst->sync, on_server, is_controller)) {
      any = true;
      break;
    }
  }
  if (!any) {
    return;
  }
  b3World_Step(mod_scene_physics_world_id(), fixed_dt, 4);
  // agent: composer-2.5 | 2026-07-30 | physics emit vel sleep | 530cd0
  const bool lockstep = mod_scene_physics_is_lockstep();
  const float sleep_lin = 0.02f;
  const float sleep_ang = 0.02f;
  for (int i = 0; i < n; i++) {
    NgSceneInst *inst = (NgSceneInst *)mod_scene_graph_inst_at(i);
    if (!inst || inst->body_id_bits == 0) {
      continue;
    }
    if (!mod_scene_physics_should_simulate(inst->sync, on_server, is_controller)) {
      continue;
    }
    b3BodyId id = b3LoadBodyId(inst->body_id_bits);
    if (!b3Body_IsValid(id)) {
      continue;
    }
    b3Pos p = b3Body_GetPosition(id);
    b3Quat q = b3Body_GetRotation(id);
    b3Vec3 lv = b3Body_GetLinearVelocity(id);
    b3Vec3 av = b3Body_GetAngularVelocity(id);
    float nrot[3];
    mod_scene_physics_euler_from_quat(q, nrot);
    const float px = (float)p.x;
    const float py = (float)p.y;
    const float pz = (float)p.z;
    const float lvx = (float)lv.x;
    const float lvy = (float)lv.y;
    const float lvz = (float)lv.z;
    const float avx = (float)av.x;
    const float avy = (float)av.y;
    const float avz = (float)av.z;
    bool pose_changed = false;
    bool vel_changed = false;
    if (fabsf(inst->pos[0] - px) > 1e-5f || fabsf(inst->pos[1] - py) > 1e-5f ||
        fabsf(inst->pos[2] - pz) > 1e-5f) {
      inst->pos[0] = px;
      inst->pos[1] = py;
      inst->pos[2] = pz;
      pose_changed = true;
    }
    if (fabsf(inst->rot[0] - nrot[0]) > 1e-5f || fabsf(inst->rot[1] - nrot[1]) > 1e-5f ||
        fabsf(inst->rot[2] - nrot[2]) > 1e-5f) {
      inst->rot[0] = nrot[0];
      inst->rot[1] = nrot[1];
      inst->rot[2] = nrot[2];
      pose_changed = true;
    }
    if (fabsf(inst->lin_vel[0] - lvx) > 1e-4f || fabsf(inst->lin_vel[1] - lvy) > 1e-4f ||
        fabsf(inst->lin_vel[2] - lvz) > 1e-4f) {
      inst->lin_vel[0] = lvx;
      inst->lin_vel[1] = lvy;
      inst->lin_vel[2] = lvz;
      vel_changed = true;
    }
    if (fabsf(inst->ang_vel[0] - avx) > 1e-4f || fabsf(inst->ang_vel[1] - avy) > 1e-4f ||
        fabsf(inst->ang_vel[2] - avz) > 1e-4f) {
      inst->ang_vel[0] = avx;
      inst->ang_vel[1] = avy;
      inst->ang_vel[2] = avz;
      vel_changed = true;
    }
    if (pose_changed) {
      mod_scene_graph_registry_set_pose(inst->id, inst->pos, inst->rot, inst->scale);
    }
    if (lockstep) {
      continue;
    }
    const float lin_spd2 = lvx * lvx + lvy * lvy + lvz * lvz;
    const float ang_spd2 = avx * avx + avy * avy + avz * avz;
    const bool at_rest =
        !pose_changed && lin_spd2 < (sleep_lin * sleep_lin) && ang_spd2 < (sleep_ang * sleep_ang);
    if (at_rest) {
      continue;
    }
    if (pose_changed) {
      mod_scene_graph_mark_dirty(inst, NG_COMP_POS | NG_COMP_ROT);
    }
    if (vel_changed || pose_changed) {
      mod_scene_graph_mark_dirty(inst, NG_COMP_LIN_VEL | NG_COMP_ANG_VEL);
    }
  }
}

typedef struct NgPhysCollectCtx {
  b3BodyId ids[NG_SCENE_INST_MAX];
  int count;
} NgPhysCollectCtx;

static bool mod_scene_physics_collect_shape(b3ShapeId shapeId, void *context) {
  NgPhysCollectCtx *ctx = (NgPhysCollectCtx *)context;
  if (!ctx || ctx->count >= NG_SCENE_INST_MAX) {
    return false;
  }
  b3BodyId bid = b3Shape_GetBody(shapeId);
  if (!b3Body_IsValid(bid)) {
    return true;
  }
  for (int i = 0; i < ctx->count; i++) {
    if (B3_ID_EQUALS(ctx->ids[i], bid)) {
      return true;
    }
  }
  ctx->ids[ctx->count++] = bid;
  return true;
}

bool mod_scene_physics_export(uint8_t **out, int *out_size) {
  if (!out || !out_size) {
    return false;
  }
  *out = NULL;
  *out_size = 0;
  if (!GPHYS().world_alive || !b3World_IsValid(mod_scene_physics_world_id())) {
    NG_LOG_ERROR("lockstep: export failed — no world");
    return false;
  }
  /* Ensure names are set for rebind after restore. */
  const int n = mod_scene_graph_inst_count();
  for (int i = 0; i < n; i++) {
    NgSceneInst *inst = (NgSceneInst *)mod_scene_graph_inst_at(i);
    if (!inst || inst->body_id_bits == 0 || inst->key[0] == '\0') {
      continue;
    }
    b3BodyId id = b3LoadBodyId(inst->body_id_bits);
    if (b3Body_IsValid(id)) {
      b3Body_SetName(id, inst->key);
    }
  }
  return b3World_Save(mod_scene_physics_world_id(), out, out_size);
}

bool mod_scene_physics_import(const uint8_t *data, int size) {
  if (!data || size <= 0) {
    return false;
  }
  /* Clear old body handles; restore replaces the world image. */
  const int n = mod_scene_graph_inst_count();
  for (int i = 0; i < n; i++) {
    NgSceneInst *inst = (NgSceneInst *)mod_scene_graph_inst_at(i);
    if (inst) {
      inst->body_id_bits = 0;
    }
  }
  mod_scene_physics_destroy_world();
  if (!mod_scene_physics_ensure_world()) {
    return false;
  }
  if (!b3World_Restore(mod_scene_physics_world_id(), data, size)) {
    NG_LOG_ERROR("lockstep: b3World_Restore failed size=%d", size);
    return false;
  }
  NG_LOG_INFO("lockstep: phys import restored size=%d", size);

  NgPhysCollectCtx collect = {0};
  b3AABB aabb = b3World_GetBounds(mod_scene_physics_world_id());
  /* Expand so sleeping/static bodies at edges are included. */
  aabb.lowerBound.x -= 1000.0f;
  aabb.lowerBound.y -= 1000.0f;
  aabb.lowerBound.z -= 1000.0f;
  aabb.upperBound.x += 1000.0f;
  aabb.upperBound.y += 1000.0f;
  aabb.upperBound.z += 1000.0f;
  b3QueryFilter filter = b3DefaultQueryFilter();
  b3World_OverlapAABB(mod_scene_physics_world_id(), aabb, filter, mod_scene_physics_collect_shape,
                      &collect);

  for (int i = 0; i < n; i++) {
    NgSceneInst *inst = (NgSceneInst *)mod_scene_graph_inst_at(i);
    if (!inst || inst->body[0] == '\0' || inst->key[0] == '\0') {
      continue;
    }
    for (int j = 0; j < collect.count; j++) {
      const char *name = b3Body_GetName(collect.ids[j]);
      if (name && strcmp(name, inst->key) == 0) {
        inst->body_id_bits = b3StoreBodyId(collect.ids[j]);
        NG_LOG_INFO("lockstep: rebind key=%s", inst->key);
        break;
      }
    }
  }
  NG_LOG_INFO("lockstep: import rebound bodies from %d collected", collect.count);
  return true;
}

// agent: composer-2.5 | 2026-07-29 | lockstep sim mode physics | f77a9c
// agent: composer-2.5 | 2026-07-30 | physics export import names | 1b75f3
// agent: composer-2.5 | 2026-07-30 | lockstep join fixes logging | 4775ae
// agent: composer-2.5 | 2026-07-30 | physics emit vel sleep | 530cd0
// agent: composer-2.5 | 2026-07-30 | kinematic proxy attach drive | a64b5e
// agent: composer-2.5 | 2026-07-30 | skip view attach under lockstep | dfc161
// agent: composer-2.5 | 2026-07-30 | apply linear impulse helper | 3ee627
// agent: composer-2.5 | 2026-07-30 | apply force and torque helpers | fc5a41
