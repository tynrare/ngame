// agent: composer-2.5 | 2026-07-25 | server sim bus module | a9d62e
#ifndef MOD_SIM_H
#define MOD_SIM_H

#include "engine/ng_mod.h"
#include "world/ng_world.h"

const NgModOps *mod_sim_ops(void);
void *mod_sim_ctx(void);
NgWorld *mod_sim_world(void);
bool mod_sim_run_cmd(const NgMsg *msg, char *reply, size_t reply_cap);
// agent: composer-2.5 | 2026-07-29 | sim load helper declaration | 909c5f
#if defined(__GNUC__)
bool mod_sim_load_scene(const char *id, char *reply, size_t reply_cap) __attribute__((weak));
#else
bool mod_sim_load_scene(const char *id, char *reply, size_t reply_cap);
#endif

#endif
// agent: composer-2.5 | 2026-07-29 | sim load helper declaration | 909c5f
