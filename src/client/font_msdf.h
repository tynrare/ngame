// agent: grok-4.6 | 2026-08-30 | graph MSDF draw API | 1514a2
// agent: grok-4.6 | 2026-08-30 | draw MSDF by scope_id | a3a680
#ifndef NG_FONT_MSDF_H
#define NG_FONT_MSDF_H

#include <raylib.h>
#include <stdbool.h>
#include <stdint.h>

#define NG_MSDF_TEXT_MAX 256

typedef struct NgMsdfText {
  char text[NG_MSDF_TEXT_MAX];
  float size;
  float outline;
  Color tint;
  bool dirty;
} NgMsdfText;

/** Load TTF, generate MSDF atlas, TF + instanced shader. */
bool mod_font_msdf_ensure(void);
/** Release atlas, TF buffers, shaders. */
void mod_font_msdf_shutdown(void);
/** Init empty label (dirty). */
void ng_msdf_text_init(NgMsdfText *t);
/** Copy string and mark dirty. */
void ng_msdf_text_set(NgMsdfText *t, const char *s);
/** Clear label string. */
void ng_msdf_text_unload(NgMsdfText *t);
/** Billboard font entities in the current 3D pass. */
void mod_font_msdf_draw_world(const Camera3D *cam, uint8_t scope_id);
/** Ortho font entities after present. */
void mod_font_msdf_draw_screen(uint8_t scope_id);

#endif
// agent: grok-4.6 | 2026-08-30 | graph MSDF draw API | 1514a2
// agent: grok-4.6 | 2026-08-30 | draw MSDF by scope_id | a3a680
