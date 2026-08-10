// agent: composer-2.5 | 2026-08-10 | demote vox header API | 0cb6a2
// agent: composer-2.5 | 2026-08-10 | uniform clip prim grid API | 6269e6
#ifndef NG_RENDER_RC_WS_H
#define NG_RENDER_RC_WS_H

#include <raylib.h>
#include <stdbool.h>
#include <stdint.h>

#define NG_RC_WS_PROBE_MAX 16
#define NG_RC_WS_GI_NEAR 0.25f
#define NG_RC_WS_GI_FAR 28.0f
/** Fixed world meters for origin snap lattice. */
#define NG_RC_WS_CELL 0.4f
/** Clip cube extent = CELL × this (fixed; never cam/frustum stretch). */
#define NG_RC_WS_VOX_RES 32
/** Max analytic SDF prims (cube/sphere from describe). */
#define NG_RC_WS_PRIM_MAX 64
/** tex_prim columns: center+type, half, quat, emit+rough, albedo+metal, lit+flags. */
#define NG_RC_WS_PRIM_COLS 6
/** Uniform candidate grid resolution per clip axis. */
#define NG_RC_WS_GRID_RES 8
/** Prim index slots per grid cell (RGBA8 channels). 255 = empty. */
#define NG_RC_WS_GRID_SLOT 4
#define NG_RC_WS_GRID_CELLS (NG_RC_WS_GRID_RES * NG_RC_WS_GRID_RES * NG_RC_WS_GRID_RES)
#define NG_RC_WS_GRID_BYTES (NG_RC_WS_GRID_CELLS * NG_RC_WS_GRID_SLOT)

typedef struct NgRcWsPrim {
  float center[3];
  float half[3];
  float quat[4];
  float lit[3];
  float albedo[3];
  float emit[3];
  float roughness;
  float metalness;
  int type; /* 0 box, 1 sphere */
} NgRcWsPrim;

typedef struct NgRcWsCtx {
  bool ready;
  bool prim_tex_ready;
  bool grid_tex_ready;
  Texture2D tex_prim; /* PRIM_COLS × PRIM_MAX RGBA32F */
  Texture2D tex_grid; /* GRID_RES × (GRID_RES²) RGBA8, 4 slots/cell */
  float *prim_rgba;
  unsigned char *grid_rgba;
  NgRcWsPrim prims[NG_RC_WS_PRIM_MAX];
  int prim_count;
  float origin[3];
  float size[3];
  int probe_n;
  int dirs;
  int cascades;
  int steps;
  uint32_t scene_hash;
  int frames_since_vox;
} NgRcWsCtx;

void ng_rc_ws_init(NgRcWsCtx *ws);
void ng_rc_ws_shutdown(NgRcWsCtx *ws);
/** Ensure prim + grid textures; quality → probe_n/dirs/cascades/steps. */
bool ng_rc_ws_ensure(NgRcWsCtx *ws, int quality);
uint32_t ng_rc_ws_scene_hash(void);
/** Fill NgRcWsPrim from graph inst (pose + materials). false if skip. */
bool ng_rc_ws_inst_prim(int i, NgRcWsPrim *out);
/** Rebuild analytic SDF prims overlapping clip; upload tex_prim. */
void ng_rc_ws_rebuild_prims(NgRcWsCtx *ws);
/** Stamp prim ids into clip grid; upload tex_grid (call after rebuild_prims). */
void ng_rc_ws_rebuild_grid(NgRcWsCtx *ws);

#endif
// agent: composer-2.5 | 2026-08-10 | demote vox header API | 0cb6a2
// agent: composer-2.5 | 2026-08-10 | prim pack emit in col3 | 3eafc7
// agent: composer-2.5 | 2026-08-10 | clip drop cam bias define | 32ad68
// agent: composer-2.5 | 2026-08-10 | uniform clip prim grid API | 6269e6
