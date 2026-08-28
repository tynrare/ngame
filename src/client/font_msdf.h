// agent: grok-4.6 | 2026-08-28 | MSDF font public API | 2296d5
#ifndef NG_FONT_MSDF_H
#define NG_FONT_MSDF_H

#include <raylib.h>
#include <stdbool.h>

/** Load TTF, generate MSDF atlas, compile shader. */
bool mod_font_msdf_ensure(void);
/** Release atlas, shader, scratch. */
void mod_font_msdf_shutdown(void);
/** Billboard demo string in the current 3D pass. */
void mod_font_msdf_draw_demo_world(const Camera3D *cam);
/** Screen-space size ladder after present. */
void mod_font_msdf_draw_demo_overlay(void);

#endif
// agent: grok-4.6 | 2026-08-28 | MSDF font public API | 2296d5
