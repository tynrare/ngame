// agent: composer-2.5 | 2026-08-01 | js module catalog API | 57817e
#ifndef NG_SCENE_JSMOD_H
#define NG_SCENE_JSMOD_H

#include <stdbool.h>
#include <stddef.h>

#define NG_JSMOD_CATALOG_MAX 64
#define NG_JSMOD_ID_MAX 48
#define NG_JSMOD_PATH_MAX 192
#define NG_JSMOD_WIRE_MAX 8

void ng_jsmod_set_current_file(const char *path);
const char *ng_jsmod_current_file(void);

/* Resolve path_in to a canonical path under NG_RES_ROOT.
 * Res-root: "scenes/x.js" -> "res/scenes/x.js"
 * Relative: "./" or "../" against current_file dirname. */
bool ng_jsmod_resolve_path(const char *path_in, const char *current_file, char *out, size_t out_cap);

bool ng_jsmod_register(const char *id, const char *path_in);
const char *ng_jsmod_lookup(const char *id);

#endif
// agent: composer-2.5 | 2026-08-01 | js module catalog API | 57817e
