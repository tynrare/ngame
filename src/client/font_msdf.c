/* Runtime MSDF atlas from TTF; overlay + world quad draw.
 * Scope in: view-side font gen/draw, JS label/font entities. Scope out: sim, console.
 * Flow id: font-msdf
 * Related: src/client/render.c, src/scene/assets.c, src/scene/host.c,
 *          res/shaders/msdf_tf.vs, res/shaders/msdf_inst.vs,
 *          res/shaders/msdf_font.fs, res/fonts/
 * Gateway role: pattern | Scope id: font-msdf | Flow id: font-msdf
 *
 * font-msdf flow:
 * 1) JS describe font/model → TTF path on NgSceneModelDesc
 * 2) client → load TTF + stbtt_InitFont → font metrics
 * 3) client → flatten glyph outlines → colored edges
 * 4) client → raster MSDF cell → packed RGB atlas
 * 5) client → upload atlas + TF + instanced shader → ready
 * 6) dirty graph inst text → compact glyphs into screen/world pool
 * 7) TF POINTS → instance mat4
 * 8) one instanced unit-quad draw per space pass
 * Branches: empty glyphs skip raster; ensure is once; draw no-ops if !ready.
 * Invariant: no malloc on draw; generate only on first ensure.
 */
// agent: grok-4.6 | 2026-08-28 | runtime MSDF atlas generate | 930b5e
#include "font_msdf.h"
#include "ng_shader.h"
#include "scene/assets.h"
#include "scene/graph.h"
#include <math.h>
#include <raymath.h>
#include <rlgl.h>
#include <stdio.h>
#include <string.h>
#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#elif defined(_WIN32)
// raylib initializes these function pointers when it creates the GL context.
// Windows opengl32 only exports OpenGL 1.1 entry points directly.
#include "glad.h"
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/glcorearb.h>
#endif

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
#define FONT_MSDF_POOL 1024

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
  NgShader inst;
  int loc_mvp;
  int loc_label;
  Mesh quad;
  unsigned int tf_program;
  unsigned int tf_vao;
  unsigned int tf_input;
  unsigned int tf_color;
  unsigned int tf_uv;
  unsigned int tf_outline;
  unsigned int tf_output;
  unsigned int tf_tfbo;
  int tf_cap;
  float pack_xywh[FONT_MSDF_POOL * 4];
  float pack_rgb[FONT_MSDF_POOL * 3];
  float pack_uv[FONT_MSDF_POOL * 4];
  float pack_ol[FONT_MSDF_POOL];
  int pool_n;
  bool pool_dirty;
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
  // agent: grok-4.6 | 2026-08-30 | graph MSDF screen entity draw | 4a12c8
  char ttf_path[160];
  const char *src = mod_scene_assets_first_font_src();
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

static char *msdf_prepend_version(const char *source, bool fragment) {
  const char *header;
#if defined(PLATFORM_DESKTOP) && !defined(GRAPHICS_API_OPENGL_ES2)
  (void)fragment;
  header = "#version 330\n";
#elif defined(GRAPHICS_API_OPENGL_ES3) || defined(__EMSCRIPTEN__)
  if (fragment) {
    header = "#version 300 es\n"
             "precision mediump float;\n";
  } else {
    header = "#version 300 es\n";
  }
#else
  (void)fragment;
  header = "#version 100\n";
#endif
  const size_t header_len = strlen(header);
  const size_t source_len = strlen(source);
  char *out = (char *)MemAlloc(header_len + source_len + 1);
  if (!out) {
    return NULL;
  }
  strcpy(out, header);
  strcat(out, source);
  return out;
}

static unsigned int msdf_compile_gl(unsigned int type, const char *source) {
  unsigned int shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, NULL);
  glCompileShader(shader);
  int ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(shader, sizeof(log), NULL, log);
    TraceLog(LOG_ERROR, "NG: MSDF TF compile failed:\n%s", log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

/** Load rasterizer-discard TF program (compact glyph → mat4). */
static unsigned int msdf_load_tf_program(const char *vs_path) {
  char *vs_src = LoadFileText(vs_path);
  if (!vs_src) {
    TraceLog(LOG_ERROR, "NG: MSDF TF vs missing: %s", vs_path);
    return 0;
  }
  char *vs_full = msdf_prepend_version(vs_src, false);
  UnloadFileText(vs_src);
  if (!vs_full) {
    return 0;
  }
  unsigned int vs = msdf_compile_gl(GL_VERTEX_SHADER, vs_full);
  MemFree(vs_full);
  if (!vs) {
    return 0;
  }
  const char *fs_body = "void main(){}\n";
  char *fs_full = msdf_prepend_version(fs_body, true);
  if (!fs_full) {
    glDeleteShader(vs);
    return 0;
  }
  unsigned int fs = msdf_compile_gl(GL_FRAGMENT_SHADER, fs_full);
  MemFree(fs_full);
  if (!fs) {
    glDeleteShader(vs);
    return 0;
  }
  unsigned int program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  const char *varyings[] = {"out_col0", "out_col1", "out_col2", "out_col3"};
  glTransformFeedbackVaryings(program, 4, varyings, GL_INTERLEAVED_ATTRIBS);
  glLinkProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  int ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetProgramInfoLog(program, sizeof(log), NULL, log);
    TraceLog(LOG_ERROR, "NG: MSDF TF link failed:\n%s", log);
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

static void msdf_tf_dispose(MsdfFont *f) {
  if (f->tf_program) {
    glDeleteProgram(f->tf_program);
  }
  if (f->tf_vao) {
    glDeleteVertexArrays(1, &f->tf_vao);
  }
  if (f->tf_input) {
    glDeleteBuffers(1, &f->tf_input);
  }
  if (f->tf_color) {
    glDeleteBuffers(1, &f->tf_color);
  }
  if (f->tf_uv) {
    glDeleteBuffers(1, &f->tf_uv);
  }
  if (f->tf_outline) {
    glDeleteBuffers(1, &f->tf_outline);
  }
  if (f->tf_output) {
    glDeleteBuffers(1, &f->tf_output);
  }
#if defined(__EMSCRIPTEN__)
  if (f->tf_tfbo) {
    glDeleteTransformFeedbacks(1, &f->tf_tfbo);
  }
#endif
  f->tf_program = 0;
  f->tf_vao = 0;
  f->tf_input = 0;
  f->tf_color = 0;
  f->tf_uv = 0;
  f->tf_outline = 0;
  f->tf_output = 0;
  f->tf_tfbo = 0;
  f->tf_cap = 0;
}

static bool msdf_make_quad(Mesh *mesh) {
  *mesh = (Mesh){0};
  mesh->vertexCount = 4;
  mesh->triangleCount = 2;
  mesh->vertices = (float *)MemAlloc(12 * sizeof(float));
  mesh->texcoords = (float *)MemAlloc(8 * sizeof(float));
  mesh->indices = (unsigned short *)MemAlloc(6 * sizeof(unsigned short));
  if (!mesh->vertices || !mesh->texcoords || !mesh->indices) {
    return false;
  }
  const float verts[] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
  const float uvs[] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
  const unsigned short idx[] = {0, 1, 2, 0, 2, 3};
  memcpy(mesh->vertices, verts, sizeof(verts));
  memcpy(mesh->texcoords, uvs, sizeof(uvs));
  memcpy(mesh->indices, idx, sizeof(idx));
  UploadMesh(mesh, false);
  return mesh->vaoId != 0;
}

// agent: grok-4.6 | 2026-08-30 | MSDF TF instanced draw path | a4f57c
static bool msdf_load_gpu(MsdfFont *f) {
  f->inst = ng_shader_load(NG_RES_ROOT "shaders/msdf_inst.vs", NG_RES_ROOT "shaders/msdf_font.fs");
  if (f->inst.handle.id == 0) {
    return false;
  }
  f->loc_mvp = GetShaderLocation(f->inst.handle, "mvp");
  f->loc_label = GetShaderLocation(f->inst.handle, "ng_label");
  if (!msdf_make_quad(&f->quad)) {
    return false;
  }
  f->tf_program = msdf_load_tf_program(NG_RES_ROOT "shaders/msdf_tf.vs");
  if (!f->tf_program) {
    return false;
  }
  f->tf_cap = FONT_MSDF_POOL;
  glGenVertexArrays(1, &f->tf_vao);
  glGenBuffers(1, &f->tf_input);
  glGenBuffers(1, &f->tf_color);
  glGenBuffers(1, &f->tf_uv);
  glGenBuffers(1, &f->tf_outline);
  glBindVertexArray(f->tf_vao);
  glBindBuffer(GL_ARRAY_BUFFER, f->tf_input);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(FONT_MSDF_POOL * 4 * (int)sizeof(float)), NULL,
               GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * (int)sizeof(float), (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * (int)sizeof(float),
                        (void *)(2 * sizeof(float)));
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, f->tf_color);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(FONT_MSDF_POOL * 3 * (int)sizeof(float)), NULL,
               GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, f->tf_uv);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(FONT_MSDF_POOL * 4 * (int)sizeof(float)), NULL,
               GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, f->tf_outline);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(FONT_MSDF_POOL * (int)sizeof(float)), NULL,
               GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glGenBuffers(1, &f->tf_output);
  glBindBuffer(GL_ARRAY_BUFFER, f->tf_output);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(FONT_MSDF_POOL * 16 * (int)sizeof(float)), NULL,
               GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
#if defined(__EMSCRIPTEN__)
  glGenTransformFeedbacks(1, &f->tf_tfbo);
  glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, f->tf_tfbo);
  glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, f->tf_output);
  glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, 0);
#endif
  return true;
}

bool mod_font_msdf_ensure(void) {
  if (g_msdf.ready) {
    return true;
  }
  if (g_msdf.failed) {
    return false;
  }
  if (!msdf_generate(&g_msdf) || !msdf_load_gpu(&g_msdf)) {
    g_msdf.failed = true;
    mod_font_msdf_shutdown();
    return false;
  }
  g_msdf.ready = true;
  return true;
}

void mod_font_msdf_shutdown(void) {
  if (g_msdf.quad.vaoId) {
    UnloadMesh(g_msdf.quad);
  }
  msdf_tf_dispose(&g_msdf);
  if (g_msdf.atlas.id) {
    UnloadTexture(g_msdf.atlas);
  }
  ng_shader_unload(&g_msdf.inst);
  memset(&g_msdf, 0, sizeof(g_msdf));
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

static void msdf_pool_reset(void) {
  g_msdf.pool_n = 0;
  g_msdf.pool_dirty = true;
}

/** Append label glyphs into the shared compact pool. */
static void msdf_text_append(NgMsdfText *t, float ox, float oy, bool y_down) {
  if (!t || !mod_font_msdf_ensure()) {
    return;
  }
  t->dirty = false;
  const float k = t->size / g_msdf.em;
  const float cr = (float)t->tint.r / 255.0f;
  const float cg = (float)t->tint.g / 255.0f;
  const float cb = (float)t->tint.b / 255.0f;
  const float inv_w = 1.0f / (float)FONT_MSDF_ATLAS_W;
  const float inv_h = 1.0f / (float)FONT_MSDF_ATLAS_H;
  float pen = 0.0f;
  for (const char *p = t->text; *p && g_msdf.pool_n < FONT_MSDF_POOL; p++) {
    const MsdfGlyph *g = msdf_glyph((unsigned char)*p);
    if (!g) {
      continue;
    }
    if (g->qw > 0.0f) {
      const int i = g_msdf.pool_n;
      const float w = g->qw * k;
      const float h = g->qh * k;
      const float gx = ox + pen + g->qx0 * k;
      const float gy = y_down ? (oy - (g->qy0 + g->qh) * k) : (oy + g->qy0 * k);
      g_msdf.pack_xywh[i * 4 + 0] = gx;
      g_msdf.pack_xywh[i * 4 + 1] = gy;
      g_msdf.pack_xywh[i * 4 + 2] = w;
      g_msdf.pack_xywh[i * 4 + 3] = h;
      g_msdf.pack_rgb[i * 3 + 0] = cr;
      g_msdf.pack_rgb[i * 3 + 1] = cg;
      g_msdf.pack_rgb[i * 3 + 2] = cb;
      g_msdf.pack_uv[i * 4 + 0] = (float)g->ax * inv_w;
      g_msdf.pack_uv[i * 4 + 2] = (float)(g->ax + g->aw) * inv_w;
      // agent: grok-4.6 | 2026-08-30 | overlay ortho flip world UV | a8fb3d
      if (y_down) {
        g_msdf.pack_uv[i * 4 + 1] = (float)g->ay * inv_h;
        g_msdf.pack_uv[i * 4 + 3] = (float)(g->ay + g->ah) * inv_h;
      } else {
        g_msdf.pack_uv[i * 4 + 1] = (float)(g->ay + g->ah) * inv_h;
        g_msdf.pack_uv[i * 4 + 3] = (float)g->ay * inv_h;
      }
      g_msdf.pack_ol[i] = t->outline;
      g_msdf.pool_n++;
      g_msdf.pool_dirty = true;
    }
    pen += g->advance * k;
  }
}

static void msdf_tf_update(int n) {
  if (n <= 0 || !g_msdf.tf_program) {
    return;
  }
  glBindBuffer(GL_ARRAY_BUFFER, g_msdf.tf_input);
  glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * 4 * (int)sizeof(float)), g_msdf.pack_xywh);
  glBindBuffer(GL_ARRAY_BUFFER, g_msdf.tf_color);
  glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * 3 * (int)sizeof(float)), g_msdf.pack_rgb);
  glBindBuffer(GL_ARRAY_BUFFER, g_msdf.tf_uv);
  glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * 4 * (int)sizeof(float)), g_msdf.pack_uv);
  glBindBuffer(GL_ARRAY_BUFFER, g_msdf.tf_outline);
  glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * (int)sizeof(float)), g_msdf.pack_ol);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glEnable(GL_RASTERIZER_DISCARD);
  glUseProgram(g_msdf.tf_program);
  glBindVertexArray(g_msdf.tf_vao);
#if defined(__EMSCRIPTEN__)
  glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, g_msdf.tf_tfbo);
#endif
  // Desktop targets OpenGL 3.3: use its default transform-feedback state.
  // Named transform-feedback objects require OpenGL 4.0 (or WebGL 2).
  glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, g_msdf.tf_output);
  glBeginTransformFeedback(GL_POINTS);
  glDrawArrays(GL_POINTS, 0, n);
  glEndTransformFeedback();
  glDisable(GL_RASTERIZER_DISCARD);
#if defined(__EMSCRIPTEN__)
  glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, 0);
#else
  glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
#endif
  glBindVertexArray(0);
  glUseProgram(0);
  g_msdf.pool_dirty = false;
}

static void msdf_flush(const Matrix *label, const Matrix *mvp, bool overlay) {
  const int n = g_msdf.pool_n;
  if (n <= 0 || g_msdf.inst.handle.id == 0 || g_msdf.quad.vaoId == 0) {
    return;
  }
  if (g_msdf.pool_dirty) {
    msdf_tf_update(n);
  }
  rlDrawRenderBatchActive();
  rlEnableShader(g_msdf.inst.handle.id);
  rlEnableColorBlend();
  // agent: grok-4.6 | 2026-08-30 | disable cull on overlay pass | 389ef2
  if (overlay) {
    rlDisableBackfaceCulling();
    rlDisableDepthTest();
  }
  // agent: grok-4.6 | 2026-08-30 | overlay ortho flip world UV | a8fb3d
  int loc_mvp = g_msdf.loc_mvp;
  if (loc_mvp < 0) {
    loc_mvp = g_msdf.inst.handle.locs[SHADER_LOC_MATRIX_MVP];
  }
  if (loc_mvp >= 0) {
    SetShaderValueMatrix(g_msdf.inst.handle, loc_mvp, *mvp);
  }
  if (g_msdf.loc_label >= 0) {
    SetShaderValueMatrix(g_msdf.inst.handle, g_msdf.loc_label, *label);
  }
  rlActiveTextureSlot(0);
  rlEnableTexture(g_msdf.atlas.id);
  rlEnableVertexArray(g_msdf.quad.vaoId);

  glBindBuffer(GL_ARRAY_BUFFER, g_msdf.tf_output);
  for (int i = 0; i < 4; i++) {
    const unsigned int loc = 9 + (unsigned int)i;
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, 16 * (int)sizeof(float),
                          (void *)(i * 4 * sizeof(float)));
    glVertexAttribDivisor(loc, 1);
  }
  glBindBuffer(GL_ARRAY_BUFFER, g_msdf.tf_color);
  glEnableVertexAttribArray(13);
  glVertexAttribPointer(13, 3, GL_FLOAT, GL_FALSE, 3 * (int)sizeof(float), (void *)0);
  glVertexAttribDivisor(13, 1);
  glBindBuffer(GL_ARRAY_BUFFER, g_msdf.tf_uv);
  glEnableVertexAttribArray(14);
  glVertexAttribPointer(14, 4, GL_FLOAT, GL_FALSE, 4 * (int)sizeof(float), (void *)0);
  glVertexAttribDivisor(14, 1);
  glBindBuffer(GL_ARRAY_BUFFER, g_msdf.tf_outline);
  glEnableVertexAttribArray(15);
  glVertexAttribPointer(15, 1, GL_FLOAT, GL_FALSE, (int)sizeof(float), (void *)0);
  glVertexAttribDivisor(15, 1);

  rlDrawVertexArrayElementsInstanced(0, g_msdf.quad.triangleCount * 3, 0, n);

  for (int i = 0; i < 4; i++) {
    const unsigned int loc = 9 + (unsigned int)i;
    glDisableVertexAttribArray(loc);
    glVertexAttribDivisor(loc, 0);
  }
  glDisableVertexAttribArray(13);
  glVertexAttribDivisor(13, 0);
  glDisableVertexAttribArray(14);
  glVertexAttribDivisor(14, 0);
  glDisableVertexAttribArray(15);
  glVertexAttribDivisor(15, 0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  rlDisableVertexArray();
  rlDisableShader();
  if (overlay) {
    rlEnableBackfaceCulling();
    rlEnableDepthTest();
  }
}

static NgMsdfText g_scratch;

// agent: grok-4.6 | 2026-08-30 | graph MSDF screen entity draw | 4a12c8
static void msdf_fill_from_inst(NgMsdfText *t, const NgSceneInst *inst) {
  ng_msdf_text_init(t);
  t->size = inst->text_size > 0.0f ? inst->text_size : 16.0f;
  t->outline = inst->text_outline;
  t->tint = (Color){inst->tint_r, inst->tint_g, inst->tint_b, 255};
  ng_msdf_text_set(t, inst->text);
}

static bool msdf_inst_is_font(const NgSceneInst *inst) {
  const NgSceneModelDesc *m = inst ? mod_scene_assets_get_model(inst->model) : NULL;
  return m && m->draw == NG_SCENE_DRAW_MSDF;
}

void mod_font_msdf_draw_world(const Camera3D *cam, uint8_t scope_id) {
  // font-msdf step 6
  if (!cam || !mod_font_msdf_ensure()) {
    return;
  }
  Vector3 fwd = Vector3Normalize(Vector3Subtract(cam->target, cam->position));
  Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, cam->up));
  Vector3 up = Vector3CrossProduct(right, fwd);
  const Matrix mvp = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
  const int n = mod_scene_graph_inst_count();
  for (int i = 0; i < n; i++) {
    const NgSceneInst *inst = mod_scene_graph_inst_at(i);
    // agent: grok-4.6 | 2026-08-30 | draw MSDF by scope_id | 19de26
    if (!inst || inst->scope_id != scope_id || !msdf_inst_is_font(inst) || !inst->text[0]) {
      continue;
    }
    msdf_fill_from_inst(&g_scratch, inst);
    msdf_pool_reset();
    msdf_text_append(&g_scratch, 0.0f, 0.0f, false);
    const Vector3 origin = {inst->pos[0], inst->pos[1], inst->pos[2]};
    Matrix label = {
        right.x, up.x, fwd.x, origin.x, right.y, up.y, fwd.y, origin.y,
        right.z, up.z, fwd.z, origin.z, 0.0f,    0.0f, 0.0f,  1.0f,
    };
    msdf_flush(&label, &mvp, false);
  }
}

void mod_font_msdf_draw_screen(uint8_t scope_id) {
  // font-msdf step 6
  if (!mod_font_msdf_ensure()) {
    return;
  }
  msdf_pool_reset();
  const int n = mod_scene_graph_inst_count();
  for (int i = 0; i < n; i++) {
    const NgSceneInst *inst = mod_scene_graph_inst_at(i);
    // agent: grok-4.6 | 2026-08-30 | draw MSDF by scope_id | 19de26
    if (!inst || inst->scope_id != scope_id || !msdf_inst_is_font(inst) || !inst->text[0]) {
      continue;
    }
    msdf_fill_from_inst(&g_scratch, inst);
    msdf_text_append(&g_scratch, inst->pos[0], inst->pos[1], true);
  }
  const Matrix ident = MatrixIdentity();
  const Matrix mvp = MatrixOrtho(0.0, (double)GetScreenWidth(), (double)GetScreenHeight(), 0.0, -1.0, 1.0);
  msdf_flush(&ident, &mvp, true);
}

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

