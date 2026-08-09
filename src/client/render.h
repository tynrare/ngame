// agent: composer-2.5 | 2026-07-25 | client render module | g0j28e
// agent: composer-2.5 | 2026-08-09 | gbuffer debug pass API | b23e6f
#ifndef MOD_RENDER_H
#define MOD_RENDER_H

#include "engine/ng_mod.h"
#include <stdbool.h>
#include <stddef.h>

const NgModOps *mod_render_ops(void);
void *mod_render_ctx(void);
bool mod_render_has_snapshot(void);
struct NgActionResult;
void mod_render_apply_action(const struct NgActionResult *result);
struct NgSessionState;
void mod_render_apply_session(const struct NgSessionState *session);
void mod_render_snapshot_text(char *out, size_t cap);
void mod_render_visibility_text(char *out, size_t cap);

/** Set render debug path (e.g. debug.render.pass / albedo). Returns false on bad path/value. */
bool mod_render_set(const char *path, const char *value);
/** Get render debug path into out. Returns false if unknown. */
bool mod_render_get(const char *path, char *out, size_t cap);

#endif

// agent: composer-2.5 | 2026-07-25 | client render module | g0j28e
// agent: composer-2.5 | 2026-08-09 | gbuffer debug pass API | b23e6f
