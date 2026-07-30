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

// agent: composer-2.5 | 2026-07-29 | entity optional body field | da5462
// agent: composer-2.5 | 2026-07-30 | state sample ring hermite | 0ba8fd
#define NG_STATE_SAMPLE_MAX 8

typedef struct NgStateSample {
  double t;
  float pos[3];
  float rot[3];
  float lin_vel[3];
  float ang_vel[3];
} NgStateSample;

typedef struct NgSceneInst {
  bool alive;
  int handle;
  uint32_t id;
  char desc_name[32];
  char key[32];
  char model[32];
  char body[32];
  uint64_t body_id_bits;
  bool phys_proxy; /* view kinematic driven by STATE_UPDATE (sim:server) */
  NgSyncMode sync;
  float pos[3];
  float rot[3];
  float scale;
  float lin_vel[3];
  float ang_vel[3];
  double state_time;
  NgStateSample samples[NG_STATE_SAMPLE_MAX];
  uint8_t sample_count;
  uint8_t sample_head;
  float phase;
  uint32_t world_id;
  uint32_t comp_dirty;
  uint16_t last_applied_seq;
  uint16_t last_sent_seq;
  /* Host: last STATE_UPDATE payload acked by a peer — delta baseline. */
  bool have_wire_ack;
  float ack_pos[3];
  float ack_rot[3];
  float ack_scale;
  float ack_lin_vel[3];
  float ack_ang_vel[3];
  float last_sent_pos[3];
  float last_sent_rot[3];
  float last_sent_scale;
  float last_sent_lin_vel[3];
  float last_sent_ang_vel[3];
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
void mod_scene_graph_push_sample(NgSceneInst *inst, double t);
bool mod_scene_graph_sample_draw_pose(const NgSceneInst *inst, double now, float delay_s,
                                      float out_pos[3], float out_rot[3]);
void mod_scene_graph_note_sent(NgSceneInst *inst, const NgStateUpdate *update);
void mod_scene_graph_note_ack(uint32_t entity_id, uint16_t ack_seq);
bool mod_scene_graph_prepare_wire_update(NgSceneInst *inst, NgStateUpdate *inout);
float mod_scene_graph_flush_priority(const NgStateUpdate *u);
int mod_scene_graph_inst_count(void);
const NgSceneInst *mod_scene_graph_inst_at(int index);

#endif
// agent: composer-2.5 | 2026-07-29 | instance primary spawn registry | d3e238
// agent: composer-2.5 | 2026-07-29 | entity optional body field | da5462
// agent: composer-2.5 | 2026-07-30 | inst store lin ang vel | 779394
// agent: composer-2.5 | 2026-07-30 | state sample ring hermite | 0ba8fd
// agent: composer-2.5 | 2026-07-30 | state ack baseline delta | 4585d3
