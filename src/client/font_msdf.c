// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
// agent: gpt-6-astra | 2026-09-05 | route shared queue through font engine | e40c88
/* Runtime MSDF atlas from TTF; overlay + world quad draw.
 * Scope in: view-side font gen/draw, JS label/font entities. Scope out: sim, console.
 * Flow id: font-msdf
 * Related: src/client/render.c, src/scene/assets.c, src/scene/host.c,
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
 *          res/shaders/msdf_inst.vs,
 *          res/shaders/msdf_font.fs, res/fonts/
 * Gateway role: pattern | Scope id: font-msdf | Flow id: font-msdf
 *
 * font-msdf flow:
 * 1) JS describe font/model → TTF path on NgSceneModelDesc
 * 2) client → load TTF + stbtt_InitFont → font metrics
 * 3) client → flatten glyph outlines → colored edges
 * 4) client → raster MSDF cell → packed RGB atlas
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
 * 5) client → upload atlas → cached font material ready
 * 6) frame-draw step 4 → select cached font → transformed plane instances
 * 7) layout → adjacent atlas runs → preserve transparent submission order
 * 8) shape-submit step 2 → shared backend → text after composition, depth writes off
 * Branches: empty labels skip; each font initializes once; failed fonts skip until shutdown.
 * Invariant: reuse glyph scratch; allocate/generate only on first use of each font.
 */
// agent: grok-4.6 | 2026-08-28 | runtime MSDF atlas generate | 930b5e
#include "font_msdf.h"
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
#include "shape.h"
#include "ng_shader.h"
#include "scene/assets.h"
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
// agent: gpt-6-astra | 2026-09-05 | read labels from shared frame queue | 7b8118
#include "scene/draw.h"
#include <stdlib.h>
#include <math.h>
#include <raymath.h>
#include <rlgl.h>
#include <stdio.h>
#include <string.h>
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#define FONT_MSDF_FIRST 32
#define FONT_MSDF_COUNT 95
// agent: grok-4.6 | 2026-08-28 | larger MSDF glyph cells | 902d55
#define FONT_MSDF_CELL 64
#define FONT_MSDF_RANGE 4.0f
#define FONT_MSDF_SLOT 72
#define FONT_MSDF_COLS 16
#define FONT_MSDF_ROWS 6
#define FONT_MSDF_ATLAS_W (FONT_MSDF_COLS * FONT_MSDF_SLOT)
#define FONT_MSDF_ATLAS_H (FONT_MSDF_ROWS * FONT_MSDF_SLOT)
#define FONT_MSDF_MAX_EDGES 1024
#define FONT_MSDF_FLAT 8
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d

typedef struct {
  float ax, ay, bx, by;
  unsigned char color;
} MsdfEdge;

typedef struct {
  bool ok;
  int ax, ay, aw, ah;
  float advance;
  float qx0, qy0, qw, qh;
} MsdfGlyph;

typedef struct {
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
// agent: gpt-6-astra | 2026-09-05 | cache fonts by source path | 2b6b40
  bool ready;
  bool failed;
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
  char src[64];
  Texture2D atlas;
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
  float em;
  unsigned char atlas_rgb[FONT_MSDF_ATLAS_W * FONT_MSDF_ATLAS_H * 3];
  unsigned char cell_rgb[FONT_MSDF_CELL * FONT_MSDF_CELL * 3];
  MsdfGlyph glyphs[FONT_MSDF_COUNT];
} MsdfFont;

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
// agent: gpt-6-astra | 2026-09-05 | reuse font resources for queued labels | 5765c6
static MsdfFont *g_msdf_fonts[NG_SCENE_ASSET_MAX];
static MsdfFont *g_msdf_active;
#define g_msdf (*g_msdf_active)

/** @param src const char* TTF source. @return bool selected cached font; allocation only on first use. */
static bool msdf_select(const char *src) {
  if (!src || !src[0]) src = "fonts/LiberationSans-Regular.ttf";
  for (int i = 0; i < NG_SCENE_ASSET_MAX; i++) {
    MsdfFont *f = g_msdf_fonts[i];
    if (f && strcmp(f->src, src)) continue;
    if (!f) {
      f = calloc(1, sizeof(*f));
      if (!f) return false;
      snprintf(f->src, sizeof(f->src), "%s", src);
      g_msdf_fonts[i] = f;
    }
    g_msdf_active = f;
    return true;
  }
  return false;
}

static float msdf_clampf(float v, float lo, float hi) {
  if (v < lo) {
    return lo;
  }
  if (v > hi) {
    return hi;
  }
  return v;
}

static void msdf_add_edge(MsdfEdge *ed, int *n, float ax, float ay, float bx, float by) {
  if (*n >= FONT_MSDF_MAX_EDGES) {
    return;
  }
  const float dx = bx - ax;
  const float dy = by - ay;
  if (dx * dx + dy * dy < 1e-8f) {
    return;
  }
  ed[*n].ax = ax;
  ed[*n].ay = ay;
  ed[*n].bx = bx;
  ed[*n].by = by;
  ed[*n].color = (unsigned char)(1u << (*n % 3));
  (*n)++;
}

static void msdf_flat_quad(MsdfEdge *ed, int *n, float x0, float y0, float cx, float cy, float x1,
                           float y1) {
  float px = x0;
  float py = y0;
  for (int i = 1; i <= FONT_MSDF_FLAT; i++) {
    const float t = (float)i / (float)FONT_MSDF_FLAT;
    const float mt = 1.0f - t;
    const float x = mt * mt * x0 + 2.0f * mt * t * cx + t * t * x1;
    const float y = mt * mt * y0 + 2.0f * mt * t * cy + t * t * y1;
    msdf_add_edge(ed, n, px, py, x, y);
    px = x;
    py = y;
  }
}

static void msdf_flat_cubic(MsdfEdge *ed, int *n, float x0, float y0, float c0x, float c0y, float c1x,
                            float c1y, float x1, float y1) {
  float px = x0;
  float py = y0;
  for (int i = 1; i <= FONT_MSDF_FLAT; i++) {
    const float t = (float)i / (float)FONT_MSDF_FLAT;
    const float mt = 1.0f - t;
    const float x = mt * mt * mt * x0 + 3.0f * mt * mt * t * c0x + 3.0f * mt * t * t * c1x +
                    t * t * t * x1;
    const float y = mt * mt * mt * y0 + 3.0f * mt * mt * t * c0y + 3.0f * mt * t * t * c1y +
                    t * t * t * y1;
    msdf_add_edge(ed, n, px, py, x, y);
    px = x;
    py = y;
  }
}

/** Unsigned distance from point to segment. */
static float msdf_seg_ud(float px, float py, const MsdfEdge *e) {
  const float bax = e->bx - e->ax;
  const float bay = e->by - e->ay;
  const float pax = px - e->ax;
  const float pay = py - e->ay;
  const float ba2 = bax * bax + bay * bay;
  float t = (ba2 > 1e-12f) ? (pax * bax + pay * bay) / ba2 : 0.0f;
  t = msdf_clampf(t, 0.0f, 1.0f);
  const float dx = pax - bax * t;
  const float dy = pay - bay * t;
  return sqrtf(dx * dx + dy * dy);
}

/** Even-odd fill test in cell pixel space. */
static int msdf_inside(const MsdfEdge *ed, int n, float px, float py) {
  int w = 0;
  for (int i = 0; i < n; i++) {
    const float ay = ed[i].ay;
    const float by = ed[i].by;
    if ((ay > py) == (by > py)) {
      continue;
    }
    const float t = (py - ay) / (by - ay);
    if (px < ed[i].ax + t * (ed[i].bx - ed[i].ax)) {
      w++;
    }
  }
  return w & 1;
}

static void msdf_raster_cell(const MsdfEdge *ed, int n, unsigned char *rgb) {
  memset(rgb, 0, (size_t)FONT_MSDF_CELL * FONT_MSDF_CELL * 3);
  if (n <= 0) {
    return;
  }
  for (int py = 0; py < FONT_MSDF_CELL; py++) {
    for (int px = 0; px < FONT_MSDF_CELL; px++) {
      const float x = (float)px + 0.5f;
      const float y = (float)py + 0.5f;
      float best[3] = {1e9f, 1e9f, 1e9f};
      for (int i = 0; i < n; i++) {
        const float ud = msdf_seg_ud(x, y, &ed[i]);
        for (int c = 0; c < 3; c++) {
          if ((ed[i].color & (1u << c)) == 0) {
            continue;
          }
          if (ud < best[c]) {
            best[c] = ud;
          }
        }
      }
      const float sgn = msdf_inside(ed, n, x, y) ? 1.0f : -1.0f;
      unsigned char *p = rgb + ((py * FONT_MSDF_CELL + px) * 3);
      // agent: grok-4.6 | 2026-08-28 | winding sign MSDF raster | 6381c9
      for (int c = 0; c < 3; c++) {
        const float mag = (best[c] > 1e8f) ? FONT_MSDF_RANGE : best[c];
        const float v = msdf_clampf(sgn * mag / FONT_MSDF_RANGE * 0.5f + 0.5f, 0.0f, 1.0f);
        p[c] = (unsigned char)(v * 255.0f + 0.5f);
      }
    }
  }
}

static int msdf_flatten_glyph(stbtt_fontinfo *info, int glyph, MsdfEdge *ed, float ox, float oy,
                              float sc, float y1) {
  stbtt_vertex *verts = NULL;
  const int nv = stbtt_GetGlyphShape(info, glyph, &verts);
  int n = 0;
  float sx = 0.0f, sy = 0.0f, cx = 0.0f, cy = 0.0f;
  int started = 0;
  for (int i = 0; i < nv; i++) {
    const stbtt_vertex *v = &verts[i];
    const float x = ox + (float)v->x * sc;
    const float y = oy + (y1 - (float)v->y) * sc;
    if (v->type == STBTT_vmove) {
      if (started) {
        msdf_add_edge(ed, &n, cx, cy, sx, sy);
      }
      sx = cx = x;
      sy = cy = y;
      started = 1;
    } else if (v->type == STBTT_vline) {
      msdf_add_edge(ed, &n, cx, cy, x, y);
      cx = x;
      cy = y;
    } else if (v->type == STBTT_vcurve) {
      const float qx = ox + (float)v->cx * sc;
      const float qy = oy + (y1 - (float)v->cy) * sc;
      msdf_flat_quad(ed, &n, cx, cy, qx, qy, x, y);
      cx = x;
      cy = y;
    } else if (v->type == STBTT_vcubic) {
      const float c0x = ox + (float)v->cx * sc;
      const float c0y = oy + (y1 - (float)v->cy) * sc;
      const float c1x = ox + (float)v->cx1 * sc;
      const float c1y = oy + (y1 - (float)v->cy1) * sc;
      msdf_flat_cubic(ed, &n, cx, cy, c0x, c0y, c1x, c1y, x, y);
      cx = x;
      cy = y;
    }
  }
  if (started) {
    msdf_add_edge(ed, &n, cx, cy, sx, sy);
  }
  stbtt_FreeShape(info, verts);
  return n;
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
// agent: gpt-6-astra | 2026-09-05 | generate the requested font source | b93cb8
/** @param f MsdfFont* selected source and atlas storage. @return bool generated. */
static bool msdf_generate(MsdfFont *f) {
  int ttf_sz = 0;
  // agent: grok-4.6 | 2026-08-30 | graph MSDF screen entity draw | 4a12c8
  char ttf_path[160];
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
  const char *src = f->src;
  if (!src || src[0] == '\0') {
    src = "fonts/LiberationSans-Regular.ttf";
  }
  snprintf(ttf_path, sizeof(ttf_path), NG_RES_ROOT "%s", src[0] == '/' ? src + 1 : src);
  unsigned char *ttf = LoadFileData(ttf_path, &ttf_sz);
  if (!ttf || ttf_sz <= 0) {
    TraceLog(LOG_ERROR, "NG: MSDF TTF missing");
    return false;
  }
  stbtt_fontinfo info;
  if (!stbtt_InitFont(&info, ttf, 0)) {
    UnloadFileData(ttf);
    TraceLog(LOG_ERROR, "NG: MSDF TTF init failed");
    return false;
  }

  int ascent = 0, descent = 0, line_gap = 0;
  stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
  (void)line_gap;
  f->em = (float)(ascent - descent);
  if (f->em < 1.0f) {
    f->em = 1.0f;
  }

  memset(f->atlas_rgb, 0, sizeof(f->atlas_rgb));
  MsdfEdge edges[FONT_MSDF_MAX_EDGES];
  const float inner = (float)FONT_MSDF_CELL - 2.0f * FONT_MSDF_RANGE;

  for (int i = 0; i < FONT_MSDF_COUNT; i++) {
    const int cp = FONT_MSDF_FIRST + i;
    MsdfGlyph *g = &f->glyphs[i];
    const int gi = stbtt_FindGlyphIndex(&info, cp);
    int adv = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(&info, gi, &adv, &lsb);
    (void)lsb;
    g->advance = (float)adv;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    const int has_box = stbtt_GetGlyphBox(&info, gi, &x0, &y0, &x1, &y1);
    const float bw = (float)(x1 - x0);
    const float bh = (float)(y1 - y0);
    const int col = i % FONT_MSDF_COLS;
    const int row = i / FONT_MSDF_COLS;
    const int sx = col * FONT_MSDF_SLOT + (FONT_MSDF_SLOT - FONT_MSDF_CELL) / 2;
    const int sy = row * FONT_MSDF_SLOT + (FONT_MSDF_SLOT - FONT_MSDF_CELL) / 2;
    g->ax = sx;
    g->ay = sy;
    g->aw = FONT_MSDF_CELL;
    g->ah = FONT_MSDF_CELL;

    if (!has_box || bw < 1.0f || bh < 1.0f) {
      g->ok = true;
      g->qx0 = 0.0f;
      g->qy0 = 0.0f;
      g->qw = 0.0f;
      g->qh = 0.0f;
      continue;
    }

    const float sc = inner / (bw > bh ? bw : bh);
    const float ox = FONT_MSDF_RANGE + (inner - bw * sc) * 0.5f - (float)x0 * sc;
    const float oy = FONT_MSDF_RANGE + (inner - bh * sc) * 0.5f;
    const int n = msdf_flatten_glyph(&info, gi, edges, ox, oy, sc, (float)y1);
    msdf_raster_cell(edges, n, f->cell_rgb);
    for (int y = 0; y < FONT_MSDF_CELL; y++) {
      unsigned char *dst =
          f->atlas_rgb + (((sy + y) * FONT_MSDF_ATLAS_W + sx) * 3);
      memcpy(dst, f->cell_rgb + y * FONT_MSDF_CELL * 3, (size_t)FONT_MSDF_CELL * 3);
    }
    const float pad = FONT_MSDF_RANGE / sc;
    const float ink_x = FONT_MSDF_RANGE + (inner - bw * sc) * 0.5f;
    const float ink_y = FONT_MSDF_RANGE + (inner - bh * sc) * 0.5f;
    // agent: grok-4.6 | 2026-08-28 | tight MSDF glyph quads | b6bd25
    int rx = (int)(ink_x - FONT_MSDF_RANGE + 0.5f);
    int ry = (int)(ink_y - FONT_MSDF_RANGE + 0.5f);
    int rw = (int)(bw * sc + 2.0f * FONT_MSDF_RANGE + 0.5f);
    int rh = (int)(bh * sc + 2.0f * FONT_MSDF_RANGE + 0.5f);
    if (rx < 0) {
      rx = 0;
    }
    if (ry < 0) {
      ry = 0;
    }
    if (rx + rw > FONT_MSDF_CELL) {
      rw = FONT_MSDF_CELL - rx;
    }
    if (ry + rh > FONT_MSDF_CELL) {
      rh = FONT_MSDF_CELL - ry;
    }
    g->ax = sx + rx;
    g->ay = sy + ry;
    g->aw = rw > 1 ? rw : 1;
    g->ah = rh > 1 ? rh : 1;
    g->qx0 = (float)x0 - pad;
    g->qy0 = (float)y0 - pad;
    g->qw = bw + 2.0f * pad;
    g->qh = bh + 2.0f * pad;
    g->ok = true;
  }

  Image img = {
      .data = f->atlas_rgb,
      .width = FONT_MSDF_ATLAS_W,
      .height = FONT_MSDF_ATLAS_H,
      .mipmaps = 1,
      .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8,
  };
  f->atlas = LoadTextureFromImage(img);
  SetTextureFilter(f->atlas, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(f->atlas, TEXTURE_WRAP_CLAMP);
  UnloadFileData(ttf);
  return f->atlas.id != 0;
}

static const MsdfGlyph *msdf_glyph(int cp) {
  const int i = cp - FONT_MSDF_FIRST;
  if (i < 0 || i >= FONT_MSDF_COUNT || !g_msdf.glyphs[i].ok) {
    return NULL;
  }
  return &g_msdf.glyphs[i];
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
static NgShader glyph_shader;
static NgShapeInstance *glyph_instances;
static int glyph_count, glyph_capacity;
/** @param need int glyph count. @return bool retained scratch capacity available. */
static bool msdf_reserve(int need) {
  if (need <= glyph_capacity) return true;
  int cap=glyph_capacity?glyph_capacity:64; while(cap<need) cap*=2;
  void *next=realloc(glyph_instances,(size_t)cap*sizeof(*glyph_instances));
  if(!next) return false;
  glyph_instances=next; glyph_capacity=cap; return true;
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
// agent: gpt-6-astra | 2026-09-05 | initialize selected font only once | 9b0afb
/** @return bool selected font initialized; retries wait for scene shutdown. */
bool mod_font_msdf_ensure(void) {
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
  if (!g_msdf_active && !msdf_select(mod_scene_assets_first_font_src())) return false;
  if (g_msdf.ready) {
    return true;
  }
  if (g_msdf.failed) {
    return false;
  }
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
  if (!msdf_generate(&g_msdf)) {
    g_msdf.failed = true;
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d

    return false;
  }
  g_msdf.ready = true;
  return true;
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
// agent: gpt-6-astra | 2026-09-05 | release all cached label font resources | e68a58
/** @return void; release every lazily created font and its GPU resources. */
void mod_font_msdf_shutdown(void) {
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
  for (int i = 0; i < NG_SCENE_ASSET_MAX; i++) {
    MsdfFont *f = g_msdf_fonts[i];
    if (!f) continue;
    if (f->atlas.id) UnloadTexture(f->atlas);
    free(f);
    g_msdf_fonts[i] = NULL;
  }
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
  g_msdf_active = NULL;
  ng_shader_unload(&glyph_shader);
  free(glyph_instances); glyph_instances=NULL; glyph_count=glyph_capacity=0;
  ng_shape_shutdown();
}

void ng_msdf_text_init(NgMsdfText *t) {
  if (!t) {
    return;
  }
  memset(t, 0, sizeof(*t));
  t->size = 16.0f;
  t->tint = WHITE;
  t->dirty = true;
}

void ng_msdf_text_set(NgMsdfText *t, const char *s) {
  if (!t) {
    return;
  }
  if (!s) {
    s = "";
  }
  strncpy(t->text, s, NG_MSDF_TEXT_MAX - 1);
  t->text[NG_MSDF_TEXT_MAX - 1] = '\0';
  t->dirty = true;
}

void ng_msdf_text_unload(NgMsdfText *t) {
  ng_msdf_text_init(t);
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
/** @param c const NgDrawCommand* label. @param label Matrix pose. @param y_down bool screen orientation. @return void; append full plane instances (font-msdf step 6). */
static void msdf_text_append(const NgDrawCommand *c, Matrix label, bool y_down) {
  const NgDrawOptions *o=&c->options;
  if(!msdf_reserve(glyph_count+(int)strlen(c->text))) return;
  const float k=o->size/g_msdf.em, iw=1.0f/FONT_MSDF_ATLAS_W, ih=1.0f/FONT_MSDF_ATLAS_H;
  float pen=0;
  for(const unsigned char *p=(const unsigned char *)c->text;*p;p++) {
    const MsdfGlyph *g=msdf_glyph(*p); if(!g) continue;
    if(g->qw>0) {
      float x=pen+g->qx0*k, y=y_down?-(g->qy0+g->qh)*k:g->qy0*k;
      Matrix local=MatrixMultiply(MatrixScale(g->qw*k,g->qh*k,1),MatrixTranslate(x,y,0));
      NgShapeInstance *v=&glyph_instances[glyph_count++];
      *v=ng_shape_instance(MatrixMultiply(local,label),o->tint);
      v->uv[0]=g->ax*iw; v->uv[2]=(g->ax+g->aw)*iw;
      v->uv[1]=(y_down?g->ay:g->ay+g->ah)*ih;
      v->uv[3]=(y_down?g->ay+g->ah:g->ay)*ih; v->outline=o->outline;
    }
    pen+=g->advance*k;
  }
}
/** @param atlas Texture2D binding. @param mvp Matrix camera. @return void; adjacent transparent run through shape-submit step 2. */
static void msdf_flush(Texture2D atlas, Matrix mvp) {
  if(!glyph_count) return;
  if(!glyph_shader.handle.id) glyph_shader=ng_shader_load(NG_RES_ROOT "shaders/msdf_inst.vs",NG_RES_ROOT "shaders/msdf_font.fs");
  MaterialMap maps[MATERIAL_MAP_BRDF+1]={0}; maps[MATERIAL_MAP_ALBEDO].texture=atlas;
  Material material={.shader=glyph_shader.handle,.maps=maps};
  ng_shape_draw(ng_shape_plane(),material,glyph_instances,glyph_count,mvp);
  glyph_count=0;
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
// agent: gpt-6-astra | 2026-09-05 | render persistent and immediate labels identically | 5f06b7


/** @param cam const Camera3D* world camera, NULL for pixels. @param scope_id uint8_t pass scope. @return void; frame-draw step 4 and font-msdf steps 6–8. */
static void msdf_draw_queue(const Camera3D *cam, uint8_t scope_id) {
  const NgDrawQueue *q = ng_draw_queue();
  const Matrix mvp = cam ? MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection()) :
    MatrixOrtho(0, GetScreenWidth(), GetScreenHeight(), 0, -1, 1);
  Vector3 fwd = {0, 0, 1}, right = {1, 0, 0}, up = {0, 1, 0};
  if (cam) {
    fwd = Vector3Normalize(Vector3Subtract(cam->target, cam->position));
    right = Vector3Normalize(Vector3CrossProduct(fwd, cam->up));
    up = Vector3CrossProduct(right, fwd);
  }
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
  Texture2D atlas={0}; uint32_t material_id=0; glyph_count=0;
  rlDrawRenderBatchActive();
  rlDisableDepthMask(); rlEnableColorBlend();
  if(!cam) { rlDisableDepthTest(); rlDisableBackfaceCulling(); }
  for (int i = 0; i < q->count; i++) {
    const NgDrawCommand *c = &q->commands[i];
    const NgDrawOptions *o = &c->options;
    if (c->kind != NG_DRAW_LABEL || o->scope_id != scope_id || !c->text[0]) continue;
    const NgSceneMaterialDesc *font = ng_material_get(o->material);
    if (!font || !msdf_select(font->font_src) || !mod_font_msdf_ensure()) continue;
    if(atlas.id && material_id!=o->material.id) msdf_flush(atlas,mvp);
    material_id=o->material.id;
    atlas=g_msdf.atlas;
    Matrix basis = {
      right.x, up.x, fwd.x, o->position[0], right.y, up.y, fwd.y, o->position[1],
      right.z, up.z, fwd.z, o->position[2], 0, 0, 0, 1,
    };
    Quaternion rotation = QuaternionFromEuler(o->rotation[0], o->rotation[1], o->rotation[2]);
    Matrix label = MatrixMultiply(MatrixMultiply(MatrixScale(o->scale[0], o->scale[1], o->scale[2]),
      QuaternionToMatrix(rotation)), basis);
    msdf_text_append(c,label,!cam);
  }
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
  msdf_flush(atlas,mvp);
  rlEnableDepthMask();
  if(!cam) { rlEnableDepthTest(); rlEnableBackfaceCulling(); }
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
/** @param cam const Camera3D* camera. @param scope_id uint8_t world scope. @return void. */
void mod_font_msdf_draw_world(const Camera3D *cam, uint8_t scope_id) {
  if (cam) msdf_draw_queue(cam, scope_id);
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
/** @param scope_id uint8_t screen scope. @return void. */
void mod_font_msdf_draw_screen(uint8_t scope_id) {
  msdf_draw_queue(NULL, scope_id);
}
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 7e5d4d
// agent: gpt-6-astra | 2026-09-05 | route shared queue through font engine | e40c88
// agent: gpt-6-astra | 2026-09-05 | read labels from shared frame queue | 7b8118
// agent: gpt-6-astra | 2026-09-05 | cache fonts by source path | 2b6b40
// agent: gpt-6-astra | 2026-09-05 | reuse font resources for queued labels | 5765c6
// agent: gpt-6-astra | 2026-09-05 | generate the requested font source | b93cb8
// agent: gpt-6-astra | 2026-09-05 | initialize selected font only once | 9b0afb
// agent: gpt-6-astra | 2026-09-05 | release all cached label font resources | e68a58
// agent: gpt-6-astra | 2026-09-05 | render persistent and immediate labels identically | 5f06b7

// agent: grok-4.6 | 2026-08-28 | runtime MSDF atlas generate | 930b5e
// agent: grok-4.6 | 2026-08-28 | winding sign MSDF raster | 6381c9
// agent: grok-4.6 | 2026-08-28 | larger MSDF glyph cells | 902d55
// agent: grok-4.6 | 2026-08-28 | tight MSDF glyph quads | b6bd25
// agent: grok-4.6 | 2026-08-30 | MSDF TF instanced draw path | a4f57c
// agent: grok-4.6 | 2026-08-30 | overlay ortho flip world UV | a8fb3d
// agent: grok-4.6 | 2026-08-30 | disable cull on overlay pass | 389ef2
// agent: grok-4.6 | 2026-08-30 | graph MSDF screen entity draw | 4a12c8
// agent: grok-4.6 | 2026-08-30 | graph MSDF screen entity draw | 4a12c8
// agent: grok-4.6 | 2026-08-30 | draw MSDF by scope_id | 19de26
