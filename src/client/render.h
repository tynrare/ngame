// agent: composer-2.5 | 2026-07-25 | client render module | g0j28e
// agent: composer-2.5 | 2026-08-09 | gbuffer debug pass API | b23e6f
// agent: composer-2.5 | 2026-08-09 | CLI set render.rc tree | ee3ab2
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
/** Chunk GI pages + vis (MCP probe_snapshot). */
void mod_render_probe_snapshot_text(char *out, size_t cap);

/** Set path (e.g. render.rc.quality / debug.render.pass / render.scale). Returns false on bad path/value. */
bool mod_render_set(const char *path, const char *value);
/** Get path into out. Returns false if unknown. */
bool mod_render_get(const char *path, char *out, size_t cap);

// agent: grok-4.6 | 2026-08-31 | screenshot request API | 9c40fa
void mod_render_request_screenshot(const char *path);
void mod_render_flush_screenshot(void);

#endif

// agent: grok-4.6 | 2026-08-21 | chunk pages in probe snapshot | 93a62c
// agent: composer-2.5 | 2026-07-25 | client render module | g0j28e
// agent: composer-2.5 | 2026-08-09 | gbuffer debug pass API | b23e6f
// agent: composer-2.5 | 2026-08-09 | CLI set render.rc tree | ee3ab2
// agent: grok-4.6 | 2026-08-12 | probe snapshot MCP header | d8a868
// agent: grok-4.6 | 2026-08-31 | screenshot request API | 9c40fa
