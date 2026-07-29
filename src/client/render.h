// agent: composer-2.5 | 2026-07-25 | client render module | g0j28e
#ifndef MOD_RENDER_H
#define MOD_RENDER_H

#include "engine/ng_mod.h"

const NgModOps *mod_render_ops(void);
void *mod_render_ctx(void);
bool mod_render_has_snapshot(void);
struct NgActionResult;
void mod_render_apply_action(const struct NgActionResult *result);
struct NgSessionState;
void mod_render_apply_session(const struct NgSessionState *session);
void mod_render_snapshot_text(char *out, size_t cap);
void mod_render_visibility_text(char *out, size_t cap);

#endif
