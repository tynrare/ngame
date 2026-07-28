// agent: composer-2.5 | 2026-07-28 | optional native scene plugins | d7e8f9
#ifndef MOD_SCENE_NATIVE_H
#define MOD_SCENE_NATIVE_H

#include <stdbool.h>

bool mod_scene_native_load(const char *scene_id);
void mod_scene_native_unload(void);
void mod_scene_native_step(float dt);
const char *mod_scene_native_banner(void);
bool mod_scene_native_active(void);

#endif

// agent: composer-2.5 | 2026-07-28 | optional native scene plugins | d7e8f9
