// agent: composer-2.5 | 2026-07-25 | embedded shared world bind | c8a1f3
#ifndef NG_EMBED_H
#define NG_EMBED_H

#include "world/ng_world.h"
#include <stdbool.h>

void ng_embed_bind(NgWorld *world);
const NgWorld *ng_embed_world(void);
bool ng_embed_ready(void);

#endif
