// agent: composer-2.5 | 2026-07-25 | agent TCP JSON bridge | h1k39f
#ifndef MOD_AGENT_H
#define MOD_AGENT_H

#include "core/ng_mod.h"

#define NG_AGENT_DEFAULT_PORT 27100

const NgModOps *mod_agent_ops(void);
void *mod_agent_ctx(void);

#endif
