// agent: composer-2.5 | 2026-07-27 | embed ready via js scene | f9a0b1
#include "ng_embed.h"
#include "scene/scene.h"
#include <stddef.h>

static NgWorld *g_world = NULL;

void ng_embed_bind(NgWorld *world) { g_world = world; }

const NgWorld *ng_embed_world(void) { return g_world; }

bool ng_embed_ready(void) { return mod_scene_is_loaded(); }
