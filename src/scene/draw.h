// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | f37770
// agent: gpt-6-astra | 2026-09-05 | define shared frame draw API | 086cf3
#ifndef NG_SCENE_DRAW_H
#define NG_SCENE_DRAW_H

#include "scene/assets.h"
#include <stdbool.h>
#include <stdint.h>

#define NG_DRAW_MAX 4096
#define NG_DRAW_TEXT_MAX 256

typedef struct NgDrawOptions {
  NgMaterialHandle material; /* Zero selects geometry default. */
  float position[3];
  float rotation[3]; /* Euler radians, matching scene transforms. */
  float scale[3];
  uint8_t tint[3]; /* Multiplies the model material; shapes are opaque. */
  uint8_t scope_id;
  float size; /* Label height: world units or screen pixels. */
  float outline; /* MSDF distance, 0..0.5. */
} NgDrawOptions;

typedef enum NgDrawKind { NG_DRAW_SHAPE, NG_DRAW_LABEL } NgDrawKind;
typedef struct NgDrawCommand {
  NgDrawKind kind;
  NgDrawOptions options;
  char model[32];
  char text[NG_DRAW_TEXT_MAX];
  int graph_index; /* -1 for transient commands, otherwise RC visibility identity. */
} NgDrawCommand;

typedef struct NgDrawQueue {
  NgDrawCommand commands[NG_DRAW_MAX];
  int count;
  unsigned rejected;
} NgDrawQueue;

/** @return NgDrawOptions identity transform, white tint, default scope, size 16. */
NgDrawOptions ng_draw_options(void);
/** Queue a model or physics shape for one frame. @param name const char* model or shape name (shape: forces physics). @param options const NgDrawOptions* or NULL defaults. @return bool accepted. */
bool ng_draw_shape(const char *name, const NgDrawOptions *options);
/** Copy a label into the same frame queue. @param font const char* described font. @param text const char* at most 255 bytes. @param options const NgDrawOptions* or NULL defaults. @return bool accepted. */
bool ng_draw_label(const char *font, const char *text, const NgDrawOptions *options);
/** Append scene objects, including model-less physics bodies. @param now double sample time. @return void. */
void ng_draw_collect_graph(double now);
/** Read active runtime's immutable queue. @return const NgDrawQueue* valid until reset. */
const NgDrawQueue *ng_draw_queue(void);
/** End frame or unload active runtime; retain storage. @return void. */
void ng_draw_reset(void);
// agent: gpt-6-astra | 2026-09-05 | declare JavaScript draw registration | d91c0a
struct duk_hthread;
/** @param ctx struct duk_hthread* scene heap. @return void. */
void ng_draw_bind(struct duk_hthread *ctx);

/** Run view scene/module/entity draw callbacks once. @return void. */
void mod_scene_draw(void);

#endif
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | f37770
// agent: gpt-6-astra | 2026-09-05 | define shared frame draw API | 086cf3
// agent: gpt-6-astra | 2026-09-05 | declare JavaScript draw registration | d91c0a
