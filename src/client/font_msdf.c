/* Runtime MSDF atlas from TTF; overlay + world quad draw.
 * Scope in: view-side font gen/draw. Scope out: sim, JS text API, console.
 * Flow id: font-msdf
 * Related: src/client/render.c, res/shaders/msdf_font.fs, res/fonts/
 * Gateway role: pattern | Scope id: font-msdf | Flow id: font-msdf
 *
 * font-msdf flow:
 * 1) client → load TTF + stbtt_InitFont → font metrics
 * 2) client → flatten glyph outlines → colored edges
 * 3) client → raster MSDF cell → packed RGB atlas
 * 4) client → upload texture + shader → ready
 * 5) render → expand string to quads → overlay or billboard draw
 * Branches: empty glyphs skip raster; ensure is once; draw no-ops if !ready.
 * Invariant: no malloc on draw; generate only on first ensure.
 */
// agent: grok-4.6 | 2026-08-28 | runtime MSDF atlas generate | 930b5e
#include "font_msdf.h"
#include "ng_shader.h"
#include <math.h>
#include <raymath.h>
#include <stdio.h>
#include <string.h>

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
#define FONT_MSDF_MAX_QUADS 256

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
  bool ready;
  bool failed;
  Texture2D atlas;
  NgShader shader;
  int loc_outline;
  int loc_tint;
  float em;
  unsigned char atlas_rgb[FONT_MSDF_ATLAS_W * FONT_MSDF_ATLAS_H * 3];
  unsigned char cell_rgb[FONT_MSDF_CELL * FONT_MSDF_CELL * 3];
  MsdfGlyph glyphs[FONT_MSDF_COUNT];
} MsdfFont;

static MsdfFont g_msdf;

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

static bool msdf_generate(MsdfFont *f) {
  int ttf_sz = 0;
  unsigned char *ttf = LoadFileData(NG_RES_ROOT "fonts/LiberationSans-Regular.ttf", &ttf_sz);
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

static bool msdf_load_shader(MsdfFont *f) {
  f->shader = ng_shader_load(NG_RES_ROOT "shaders/fullscreen.vs",
                             NG_RES_ROOT "shaders/msdf_font.fs");
  if (f->shader.handle.id == 0) {
    return false;
  }
  f->loc_outline = GetShaderLocation(f->shader.handle, "ng_outline");
  f->loc_tint = GetShaderLocation(f->shader.handle, "ng_tint");
  return true;
}

bool mod_font_msdf_ensure(void) {
  if (g_msdf.ready) {
    return true;
  }
  if (g_msdf.failed) {
    return false;
  }
  if (!msdf_generate(&g_msdf) || !msdf_load_shader(&g_msdf)) {
    g_msdf.failed = true;
    mod_font_msdf_shutdown();
    return false;
  }
  g_msdf.ready = true;
  return true;
}

void mod_font_msdf_shutdown(void) {
  if (g_msdf.atlas.id) {
    UnloadTexture(g_msdf.atlas);
  }
  ng_shader_unload(&g_msdf.shader);
  memset(&g_msdf, 0, sizeof(g_msdf));
}

static const MsdfGlyph *msdf_glyph(int cp) {
  const int i = cp - FONT_MSDF_FIRST;
  if (i < 0 || i >= FONT_MSDF_COUNT || !g_msdf.glyphs[i].ok) {
    return NULL;
  }
  return &g_msdf.glyphs[i];
}

// agent: grok-4.6 | 2026-08-28 | MSDF draw via TexturePro | b5a0f0
/** Bind MSDF shader and tint/outline uniforms. */
static void msdf_begin(float outline, Color tint) {
  const float o = outline;
  const float t[3] = {(float)tint.r / 255.0f, (float)tint.g / 255.0f, (float)tint.b / 255.0f};
  BeginShaderMode(g_msdf.shader.handle);
  if (g_msdf.loc_outline >= 0) {
    SetShaderValue(g_msdf.shader.handle, g_msdf.loc_outline, &o, SHADER_UNIFORM_FLOAT);
  }
  if (g_msdf.loc_tint >= 0) {
    SetShaderValue(g_msdf.shader.handle, g_msdf.loc_tint, t, SHADER_UNIFORM_VEC3);
  }
}

static Rectangle msdf_src(const MsdfGlyph *g) {
  return (Rectangle){(float)g->ax, (float)g->ay, (float)g->aw, (float)g->ah};
}

/** Screen-space string; `y` is baseline. */
static void msdf_draw_2d(const char *s, float x, float y, float px, Color c, float outline) {
  if (!s || !mod_font_msdf_ensure()) {
    return;
  }
  const float k = px / g_msdf.em;
  float pen = x;
  int n = 0;
  msdf_begin(outline, c);
  for (const char *p = s; *p && n < FONT_MSDF_MAX_QUADS; p++) {
    const MsdfGlyph *g = msdf_glyph((unsigned char)*p);
    if (!g) {
      continue;
    }
    if (g->qw > 0.0f) {
      const float w = g->qw * k;
      const float h = g->qh * k;
      const Rectangle dst = {pen + g->qx0 * k, y - (g->qy0 + g->qh) * k, w, h};
      DrawTexturePro(g_msdf.atlas, msdf_src(g), dst, (Vector2){0.0f, 0.0f}, 0.0f, c);
      n++;
    }
    pen += g->advance * k;
  }
  EndShaderMode();
}

/** Camera-facing string; `origin` is left baseline. */
static void msdf_draw_world(const char *s, Vector3 origin, float world_h, Color c, const Camera3D *cam) {
  if (!s || !cam || !mod_font_msdf_ensure()) {
    return;
  }
  Vector3 fwd = Vector3Normalize(Vector3Subtract(cam->target, cam->position));
  Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, cam->up));
  Vector3 up = Vector3CrossProduct(right, fwd);
  const float k = world_h / g_msdf.em;
  float pen = 0.0f;
  int n = 0;
  msdf_begin(0.0f, c);
  for (const char *p = s; *p && n < FONT_MSDF_MAX_QUADS; p++) {
    const MsdfGlyph *g = msdf_glyph((unsigned char)*p);
    if (!g) {
      continue;
    }
    if (g->qw > 0.0f) {
      const float w = g->qw * k;
      const float h = g->qh * k;
      const float mx = pen + (g->qx0 + g->qw * 0.5f) * k;
      const float my = (g->qy0 + g->qh * 0.5f) * k;
      const Vector3 pos =
          Vector3Add(origin, Vector3Add(Vector3Scale(right, mx), Vector3Scale(up, my)));
      DrawBillboardPro(*cam, g_msdf.atlas, msdf_src(g), pos, cam->up, (Vector2){w, h},
                       (Vector2){w * 0.5f, h * 0.5f}, 0.0f, c);
      n++;
    }
    pen += g->advance * k;
  }
  EndShaderMode();
}

void mod_font_msdf_draw_demo_world(const Camera3D *cam) {
  // font-msdf step 5
  msdf_draw_world("MSDF", (Vector3){0.0f, 1.4f, 0.0f}, 0.45f, RAYWHITE, cam);
}

void mod_font_msdf_draw_demo_overlay(void) {
  // font-msdf step 5
  msdf_draw_2d("The quick brown fox 12.5", 24.0f, 72.0f, 22.0f, RAYWHITE, 0.0f);
  msdf_draw_2d("small 14px", 24.0f, 108.0f, 14.0f, (Color){180, 220, 255, 255}, 0.0f);
  msdf_draw_2d("large 48px", 24.0f, 168.0f, 48.0f, (Color){255, 210, 120, 255}, 0.0f);
  msdf_draw_2d("outlined", 24.0f, 220.0f, 28.0f, (Color){120, 255, 160, 255}, 0.12f);
}

// agent: grok-4.6 | 2026-08-28 | runtime MSDF atlas generate | 930b5e
// agent: grok-4.6 | 2026-08-28 | MSDF draw via TexturePro | b5a0f0
// agent: grok-4.6 | 2026-08-28 | winding sign MSDF raster | 6381c9
// agent: grok-4.6 | 2026-08-28 | larger MSDF glyph cells | 902d55
// agent: grok-4.6 | 2026-08-28 | tight MSDF glyph quads | b6bd25
