// agent: composer-2.5 | 2026-08-10 | WS vox upload API | 9c8b94
#ifndef NG_RENDER_RC_WS_H
#define NG_RENDER_RC_WS_H

#include <raylib.h>
#include <stdbool.h>
#include <stdint.h>

#define NG_RC_WS_VOX_RES 32
#define NG_RC_WS_PROBE_MAX 16

typedef struct NgRcWsCtx {
  bool ready;
  bool vox_tex_ready;
  Texture2D tex_vox; /* VOX_RES × (VOX_RES*VOX_RES); RGB=lit, A=occ */
  unsigned char *vox_rgba; /* upload scratch */
  unsigned char *vox_occ;
  unsigned char *vox_alb; /* RGB */
  unsigned char *vox_emit; /* RGB */
  float origin[3];
  float size[3];
  int probe_n; /* N for N×N×N probe atlas */
  int dirs;
  int cascades;
  int steps;
  uint32_t scene_hash;
  int frames_since_vox;
} NgRcWsCtx;

/** Init zeros. */
void ng_rc_ws_init(NgRcWsCtx *ws);
void ng_rc_ws_shutdown(NgRcWsCtx *ws);
/** Ensure vox buffers/texture; quality → probe_n/dirs/cascades/steps. */
bool ng_rc_ws_ensure(NgRcWsCtx *ws, int quality);
/** Voxelize when dirty; return true if atlas bytes changed. */
bool ng_rc_ws_sync_vox(NgRcWsCtx *ws);
/** Pack vox → tex_vox slice atlas. */
void ng_rc_ws_upload_vox(NgRcWsCtx *ws);

#endif
// agent: composer-2.5 | 2026-08-10 | WS vox upload API | 9c8b94
