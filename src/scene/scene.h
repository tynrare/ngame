// agent: composer-2.5 | 2026-07-27 | js scene lifecycle host | f3a4b5
#ifndef MOD_SCENE_H
#define MOD_SCENE_H

#include "engine/ng_mod.h"
#include "engine/ng_session.h"
#include "engine/ng_sync.h"
#include "world/ng_world.h"
#include <stdbool.h>

// agent: codex-5.3 | 2026-07-29 | add scene W S keycodes | c2be81
#define NG_SCENE_KEY_A 1
#define NG_SCENE_KEY_D 2
#define NG_SCENE_KEY_W 3
#define NG_SCENE_KEY_S 4

const NgModOps *mod_scene_ops(void);
void *mod_scene_ctx(void);

bool mod_scene_load(const char *scene_id);
bool mod_scene_load_boot(void);
bool mod_scene_is_loaded(void);
const char *mod_scene_current_id(void);
int mod_scene_entity_count(void);
void mod_scene_fill_session(NgSessionState *session);
bool mod_scene_is_controller(void);
bool mod_scene_can_author(NgSyncMode sync);
void mod_scene_on_session(const NgSessionState *session);
void mod_scene_view_on_session(const NgSessionState *session);
void mod_scene_apply_remote(const NgStateUpdate *update);
void mod_scene_view_apply_remote(const NgStateUpdate *update);
bool mod_scene_take_flush(NgStateUpdate *out);
bool mod_scene_graph_active(void);
bool mod_scene_view_is_loaded(void);
const char *mod_scene_view_current_id(void);
int mod_scene_view_entity_count(void);
bool mod_scene_view_graph_active(void);
bool mod_scene_smoke_test(void);
void mod_scene_mirror_server(NgWorld *w);
void mod_scene_apply_snapshot(const NgSnapshot *snap);
void mod_scene_view_apply_snapshot(const NgSnapshot *snap);
void mod_scene_view_status_text(char *out, size_t cap);
// agent: composer-2.5 | 2026-07-29 | view entity transform observe | 4d8e21
void mod_scene_view_entities_text(char *out, size_t cap);
// agent: composer-2.5 | 2026-07-30 | expose server entities text | f994f2
void mod_scene_server_entities_text(char *out, size_t cap);
void mod_scene_phys_debug_text(char *out, size_t cap);
bool mod_scene_debug_apply_torque_key(const char *key, float tx, float ty, float tz);
// agent: composer-2.5 | 2026-07-29 | shared raycast plane helper | 7c1d4a
bool mod_scene_raycast_plane_y(float plane_y, float *out_x, float *out_y, float *out_z);
void mod_scene_raycast_plane_y_text(float plane_y, char *out, size_t cap);
bool mod_scene_is_native(void);

#endif
// agent: codex-5.3 | 2026-07-29 | add scene W S keycodes | c2be81
// agent: composer-2.5 | 2026-07-29 | view entity transform observe | 4d8e21
// agent: composer-2.5 | 2026-07-29 | shared raycast plane helper | 7c1d4a
