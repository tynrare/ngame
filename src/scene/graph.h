// agent: composer-2.5 | 2026-07-27 | spawn registry and routing | e2f3a4
// agent: composer-2.5 | 2026-07-29 | instance primary spawn registry | d3e238
#ifndef MOD_SCENE_GRAPH_H
#define MOD_SCENE_GRAPH_H

#include "engine/ng_session.h"
#include "engine/ng_sync.h"
#include <stdbool.h>
#include <stdint.h>

struct duk_hthread;

#define NG_SCENE_DESC_MAX 32
#define NG_SCENE_INST_MAX 64

// agent: composer-2.5 | 2026-07-29 | entity optional body field | da5462
typedef struct NgSceneDesc {
  char kind[16];
  char name[32];
  char model[32];
  char body[32];
  NgSyncMode sync;
  int func_stash_idx;
  bool alive;
} NgSceneDesc;

typedef struct NgSceneInst {
  bool alive;
  int handle;
  uint32_t id;
  char desc_name[32];
  char key[32];
  char model[32];
  char body[32];
  uint64_t body_id_bits;
  NgSyncMode sync;
  float pos[3];
  float rot[3];
  float scale;
  float phase;
  uint32_t world_id;
  uint32_t comp_dirty;
  uint16_t last_applied_seq;
  uint16_t last_sent_seq;
  int script_inst_stash;
} NgSceneInst;

typedef struct NgSceneRegistry {
  char desc_name[32];
  char key[32];
  uint32_t entity_id;
  NgSyncMode sync;
  float pos[3];
  float rot[3];
  float scale;
  bool alive;
} NgSceneRegistry;

typedef struct ModSceneGraphCtx {
  NgSceneDesc descs[NG_SCENE_DESC_MAX];
  int desc_count;
  NgSceneInst insts[NG_SCENE_INST_MAX];
  int inst_count;
  NgSceneRegistry registry[NG_SCENE_INST_MAX];
  int registry_count;
  NgSessionSpawn pending[NG_SESSION_SPAWN_MAX];
  uint8_t pending_matched[NG_SESSION_SPAWN_MAX];
  int pending_count;
  uint32_t next_local_id;
  uint16_t next_seq;
} ModSceneGraphCtx;

void mod_scene_graph_reset(void);
bool mod_scene_graph_describe(const char *kind, const char *name, NgSyncMode sync,
                              const char *model, const char *body, int func_stash_idx);
NgSceneDesc *mod_scene_graph_entity_desc(const char *name);
bool mod_scene_graph_dispose_desc(const char *kind, const char *name);
uint32_t mod_scene_graph_alloc_id(void);

NgSceneRegistry *mod_scene_graph_registry_by_id(uint32_t entity_id);
NgSceneRegistry *mod_scene_graph_registry_by_key(const char *key);
bool mod_scene_graph_registry_add_instance(const char *desc_name, const char *key,
                                           uint32_t entity_id, NgSyncMode sync, const float pos[3],
                                           const float rot[3], float scale);
void mod_scene_graph_registry_set_pose(uint32_t entity_id, const float pos[3], const float rot[3],
                                       float scale);
void mod_scene_graph_registry_clear_id(uint32_t entity_id);
void mod_scene_graph_fill_session_spawns(NgSessionState *session);

void mod_scene_graph_seed_pending(const NgSessionState *session);
const NgSessionSpawn *mod_scene_graph_take_pending(const char *desc_name, const char *key);
void mod_scene_graph_foreach_unmatched_pending(void (*fn)(const NgSessionSpawn *sp, void *ud),
                                               void *ud);

int mod_scene_graph_spawn(const char *desc_name, uint32_t entity_id, const char *key,
                          const float pos[3], const float rot[3], float scale,
                          struct duk_hthread *ctx, int func_stash_idx);
bool mod_scene_graph_despawn(int handle);
NgSceneInst *mod_scene_graph_inst_by_handle(int handle);
NgSceneInst *mod_scene_graph_inst_by_id(uint32_t entity_id);
NgSceneInst *mod_scene_graph_inst_by_key(const char *key);
NgSceneInst *mod_scene_graph_inst_by_desc(const char *desc_name);
NgSyncMode mod_scene_graph_sync_for_entity(uint32_t entity_id);
void mod_scene_graph_mark_dirty(NgSceneInst *inst, uint32_t comp);
bool mod_scene_graph_take_dirty(NgStateUpdate *out);
void mod_scene_graph_apply_update(const NgStateUpdate *update);
int mod_scene_graph_inst_count(void);
const NgSceneInst *mod_scene_graph_inst_at(int index);

#endif
// agent: composer-2.5 | 2026-07-29 | instance primary spawn registry | d3e238
// agent: composer-2.5 | 2026-07-29 | entity optional body field | da5462
