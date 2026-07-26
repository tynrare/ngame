// agent: composer-2.5 | 2026-07-25 | embedded shared world bind | c8a1f3
#include "ng_embed.h"
#include <stddef.h>

static NgWorld *g_world = NULL;

void ng_embed_bind(NgWorld *world) { g_world = world; }

const NgWorld *ng_embed_world(void) { return g_world; }

bool ng_embed_ready(void) {
  return g_world != NULL && g_world->live_count > 0;
}
