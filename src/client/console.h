// agent: composer-2.5 | 2026-07-25 | console bus module | 6b2e9a
#ifndef MOD_CONSOLE_H
#define MOD_CONSOLE_H

#include "engine/ng_mod.h"

const NgModOps *mod_console_ops(void);
void *mod_console_ctx(void);
void mod_console_poll_input(void);

#endif
