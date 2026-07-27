// agent: composer-2.5 | 2026-07-27 | js scene lifecycle host | f3a4b5
#ifndef MOD_SCENE_H
#define MOD_SCENE_H

#include "core/ng_mod.h"
#include "core/ng_session.h"
#include "core/ng_sync.h"
#include <stdbool.h>

#define NG_SCENE_KEY_A 1
#define NG_SCENE_KEY_D 2

const NgModOps *mod_scene_ops(void);
void *mod_scene_ctx(void);

bool mod_scene_load(const char *scene_id);
bool mod_scene_is_loaded(void);
const char *mod_scene_current_id(void);
int mod_scene_entity_count(void);
void mod_scene_fill_session(NgSessionState *session);
bool mod_scene_is_controller(void);
bool mod_scene_can_author(NgSyncMode sync);
void mod_scene_on_session(const NgSessionState *session);
void mod_scene_apply_remote(const NgStateUpdate *update);
bool mod_scene_take_flush(NgStateUpdate *out);
bool mod_scene_graph_active(void);
bool mod_scene_smoke_test(void);

#endif
