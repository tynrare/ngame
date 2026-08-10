// agent: composer-2.5 | 2026-08-10 | CPU frustum vox rebuild API | 2508fb
// agent: composer-2.5 | 2026-08-10 | fixed world cell for vox snap | b66afd
// agent: composer-2.5 | 2026-08-10 | rc-ws header SDF plan | 37c406
#ifndef NG_RENDER_RC_WS_H
#define NG_RENDER_RC_WS_H

#include <raylib.h>
#include <stdbool.h>
#include <stdint.h>

#define NG_RC_WS_VOX_RES 32
#define NG_RC_WS_PROBE_MAX 16
#define NG_RC_WS_GI_NEAR 0.25f
#define NG_RC_WS_GI_FAR 28.0f
/** Fixed world meters per voxel — atlas stays world-aligned (no adaptive remesh). */
#define NG_RC_WS_CELL 0.4f
/** Phase 6.2: max analytic SDF prims (cube/sphere from describe). */
#define NG_RC_WS_PRIM_MAX 64

typedef struct NgRcWsCtx {
  bool ready;
  bool vox_tex_ready;
  Texture2D tex_vox; /* VOX × VOX²; RGB=lit A=occ */
  unsigned char *vox_rgba;
  unsigned char *vox_occ;
  unsigned char *vox_alb;
  unsigned char *vox_emit;
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
/** Ensure vox scratch + tex_vox; quality → probe_n/dirs/cascades/steps. */
bool ng_rc_ws_ensure(NgRcWsCtx *ws, int quality);
uint32_t ng_rc_ws_scene_hash(void);
/**
 * Instance stamp AABB + lit RGB. false if missing/skip (floor slab).
 * lit = emit*boost + albedo*bounce (clamped).
 */
bool ng_rc_ws_inst_stamp(int i, float center[3], float half[3], float lit[3]);
/** Clear + stamp overlapping instances into atlas using ws->origin/size; upload tex_vox. */
void ng_rc_ws_rebuild_vox(NgRcWsCtx *ws);

#endif
// agent: composer-2.5 | 2026-08-10 | CPU frustum vox rebuild API | 2508fb
// agent: composer-2.5 | 2026-08-10 | fixed world cell for vox snap | b66afd
// agent: composer-2.5 | 2026-08-10 | rc-ws header SDF plan | 37c406
