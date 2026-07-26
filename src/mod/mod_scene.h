// agent: composer-2.5 | 2026-07-26 | client scene JS host | b8c9d0
#ifndef MOD_SCENE_H
#define MOD_SCENE_H

#include "core/ng_mod.h"
#include "core/ng_session.h"
#include <stdbool.h>

const NgModOps *mod_scene_ops(void);
void *mod_scene_ctx(void);

bool mod_scene_client_fields_active(void);
bool mod_scene_is_controller(void);
void mod_scene_on_session(const NgSessionState *session);
void mod_scene_apply_remote(const NgStateUpdate *update);
bool mod_scene_take_flush(NgStateUpdate *out);
float mod_scene_get_rot_y(void);

#endif
