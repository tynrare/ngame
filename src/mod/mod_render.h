// agent: composer-2.5 | 2026-07-25 | client render module | g0j28e
#ifndef MOD_RENDER_H
#define MOD_RENDER_H

#include "core/ng_mod.h"

const NgModOps *mod_render_ops(void);
void *mod_render_ctx(void);
bool mod_render_has_snapshot(void);

#endif
