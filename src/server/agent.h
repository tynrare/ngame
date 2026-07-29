// agent: composer-2.5 | 2026-07-25 | agent TCP JSON bridge | h1k39f
#ifndef MOD_AGENT_H
#define MOD_AGENT_H

#include "engine/ng_mod.h"

#define NG_AGENT_DEFAULT_PORT 27100

const NgModOps *mod_agent_ops(void);
void *mod_agent_ctx(void);
void mod_agent_poll(void);
void mod_agent_configure(uint16_t port);
// agent: composer-2.5 | 2026-07-29 | expose agent listening port | 1b2c3d
uint16_t mod_agent_listening_port(void);

// agent: composer-2.5 | 2026-07-29 | expose agent listening port | 1b2c3d
#endif
