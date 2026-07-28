// agent: composer-2.5 | 2026-07-27 | spawn registry and routing | e2f3a4
#ifndef MOD_SCENE_GRAPH_H
#define MOD_SCENE_GRAPH_H

#include "engine/ng_session.h"
#include "engine/ng_sync.h"
#include <stdbool.h>
#include <stdint.h>

struct duk_hthread;

#define NG_SCENE_DESC_MAX 32
#define NG_SCENE_INST_MAX 64

typedef struct NgSceneDesc {
  char kind[16];
  char name[32];
  char model[32];
  NgSyncMode sync;
  int func_stash_idx;
  bool alive;
} NgSceneDesc;

typedef struct NgSceneInst {
  bool alive;
  int handle;
  uint32_t id;
  char desc_name[32];
  char model[32];
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

void mod_scene_graph_reset(void);
bool mod_scene_graph_describe(const char *kind, const char *name, NgSyncMode sync,
                              const char *model, int func_stash_idx);
NgSceneDesc *mod_scene_graph_entity_desc(const char *name);
bool mod_scene_graph_dispose_desc(const char *kind, const char *name);
uint32_t mod_scene_graph_alloc_id(void);
bool mod_scene_graph_registry_add(const char *desc_name, uint32_t entity_id, NgSyncMode sync);
uint32_t mod_scene_graph_registry_id_for_desc(const char *desc_name);
bool mod_scene_graph_registry_ensure(const char *desc_name, NgSyncMode sync, uint32_t *out_id);
void mod_scene_graph_fill_session_spawns(NgSessionState *session);
int mod_scene_graph_spawn(const char *desc_name, uint32_t entity_id, struct duk_hthread *ctx,
                          int func_stash_idx);
bool mod_scene_graph_despawn(int handle);
NgSceneInst *mod_scene_graph_inst_by_handle(int handle);
NgSceneInst *mod_scene_graph_inst_by_id(uint32_t entity_id);
NgSceneInst *mod_scene_graph_inst_by_desc(const char *desc_name);
NgSyncMode mod_scene_graph_sync_for_entity(uint32_t entity_id);
void mod_scene_graph_mark_dirty(NgSceneInst *inst, uint32_t comp);
bool mod_scene_graph_take_dirty(NgStateUpdate *out);
void mod_scene_graph_apply_update(const NgStateUpdate *update);
int mod_scene_graph_inst_count(void);
const NgSceneInst *mod_scene_graph_inst_at(int index);

#endif
