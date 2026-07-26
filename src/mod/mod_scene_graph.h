// agent: composer-2.5 | 2026-07-26 | client scene graph store | b7c8d9
#ifndef MOD_SCENE_GRAPH_H
#define MOD_SCENE_GRAPH_H

#include "core/ng_session.h"
#include "core/ng_sync.h"
#include "world/ng_world.h"
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
  uint32_t comp_dirty;
  int script_inst_stash;
} NgSceneInst;

void mod_scene_graph_reset(void);
bool mod_scene_graph_describe(const char *kind, const char *name, NgSyncMode sync,
                              const char *model, int func_stash_idx);
bool mod_scene_graph_dispose_desc(const char *kind, const char *name);
int mod_scene_graph_spawn(const char *desc_name, uint32_t entity_id, struct duk_hthread *ctx,
                          int func_stash_idx);
bool mod_scene_graph_despawn(int handle);
NgSceneInst *mod_scene_graph_inst_by_handle(int handle);
NgSceneInst *mod_scene_graph_inst_by_id(uint32_t entity_id);
NgSyncMode mod_scene_graph_sync_for_entity(uint32_t entity_id);
void mod_scene_graph_mark_dirty(NgSceneInst *inst, uint32_t comp);
bool mod_scene_graph_take_dirty(NgStateUpdate *out);
void mod_scene_graph_apply_update(const NgStateUpdate *update);
int mod_scene_graph_inst_count(void);
const NgSceneInst *mod_scene_graph_inst_at(int index);
float mod_scene_graph_primary_rot_y(void);
bool mod_scene_graph_has_client_entities(void);

#endif
