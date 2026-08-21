// agent: grok-4.6 | 2026-08-21 | drop sparse grid probe API | db96aa
#ifndef NG_RENDER_RC_WS_H
#define NG_RENDER_RC_WS_H

#include <raylib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Finest world meters. Chunk = N³ × CELL (8 m). */
#define NG_RC_WS_CELL 1.0f
/** Max cullable analytic SDF prims (matches scene inst cap). */
#define NG_RC_WS_PRIM_MAX 2048
#define NG_RC_WS_INST_MAX 2048
#define NG_RC_WS_AABB_PAD 0.05f
#define NG_RC_WS_CULL_STACK 64
/** tex_prim_vis upload bytes (PRIM_MAX × 2 rows RGBA8). */
#define NG_RC_WS_PRIM_VIS_BYTES (NG_RC_WS_PRIM_MAX * 2 * 4)
/** tex_prim columns: center+type, half, quat, emit+rough, albedo+metal, lit+inst, leaf+pad. */
#define NG_RC_WS_PRIM_COLS 7
/** World-fixed chunk lattice (probe identity). N³ cells; h from UVW. */
#define NG_RC_WS_CHUNK_N 8
#define NG_RC_WS_CHUNK_CELLS (NG_RC_WS_CHUNK_N * NG_RC_WS_CHUNK_N * NG_RC_WS_CHUNK_N)
#define NG_RC_WS_CHUNK_VIS_MAX 64
#define NG_RC_WS_PAGE_CAP NG_RC_WS_CHUNK_VIS_MAX
#define NG_RC_WS_PAGE_HASH 128
#define NG_RC_WS_PAGE_HASH_PROBE 8
#define NG_RC_WS_PAGE_FILL_MAX 2
/** Flat binary BVH capacity (~2N-1 for PRIM_MAX leaves). */
#define NG_RC_WS_BVH_MAX 4096
/** tex_bvh columns: bmin+left, bmax+right, prim+parent, depth+pad. */
#define NG_RC_WS_BVH_COLS 4
#define NG_RC_WS_BVH_FLOATS (NG_RC_WS_BVH_MAX * NG_RC_WS_BVH_COLS * 4)
#define NG_RC_WS_INST_PRIM_FLOATS (NG_RC_WS_INST_MAX * 4)
#define NG_RC_WS_CULL_VIS_WORDS ((NG_RC_WS_INST_MAX + 31) / 32)

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
  int inst_i; /* graph instance index */
  int leaf_node; /* BVH leaf node index */
} NgRcWsPrim;

/** World-fixed chunk index (cx,cy,cz). */
typedef struct NgRcWsChunkId {
  int32_t cx;
  int32_t cy;
  int32_t cz;
} NgRcWsChunkId;

/** GPU page atlas; keyed by world chunk. */
typedef struct NgRcWsPage {
  int32_t cx;
  int32_t cy;
  int32_t cz;
  uint8_t occupied;
  uint8_t dirty;
  uint32_t last_use;
} NgRcWsPage;

/** CPU BVH node: leaf has prim>=0; internal has left/right child indices. */
typedef struct NgRcWsBvhNode {
  float bmin[3];
  float bmax[3];
  int32_t left;
  int32_t right;
  int32_t prim; /* >=0 leaf prim index; -1 internal */
  int32_t parent; /* -1 root */
  int32_t depth;
} NgRcWsBvhNode;

typedef struct NgRcWsCtx {
  bool ready;
  bool prim_tex_ready;
  bool bvh_tex_ready;
  bool inst_prim_tex_ready;
  Texture2D tex_prim; /* PRIM_COLS × PRIM_MAX RGBA32F */
  Texture2D tex_bvh; /* BVH_COLS × BVH_MAX RGBA32F */
  Texture2D tex_inst_prim; /* 1 × INST_MAX prim+1 per inst (0 = none) */
  float *prim_rgba;
  float *bvh_rgba;
  float *inst_prim_rgba;
  NgRcWsPrim prims[NG_RC_WS_PRIM_MAX];
  int16_t inst_prim[NG_RC_WS_INST_MAX]; /* prim row or -1 */
  NgRcWsBvhNode bvh[NG_RC_WS_BVH_MAX];
  int bvh_root; /* -1 empty */
  int bvh_count;
  int bvh_max_depth;
  int prim_count;
  uint32_t scene_hash;
  uint32_t cull_vis_bits[NG_RC_WS_CULL_VIS_WORDS];
  int cull_vis_n;
  bool cull_valid;
  uint8_t cull_prim_vis[NG_RC_WS_PRIM_MAX];
  unsigned char *prim_vis_rgba;
  NgRcWsChunkId chunk_vis[NG_RC_WS_CHUNK_VIS_MAX];
  int chunk_vis_n;
  Texture2D tex_chunk_vis; /* 1 × VIS_MAX RGBA32F cx,cy,cz,used */
  float *chunk_vis_rgba;
  bool chunk_vis_tex_ready;
  NgRcWsPage pages[NG_RC_WS_PAGE_CAP];
  uint32_t page_tick;
  Texture2D tex_pages; /* 1 × PAGE_CAP RGBA32F cx,cy,cz,used */
  float *pages_rgba;
  bool pages_tex_ready;
  Texture2D tex_page_hash; /* HASH × 1 RGBA32F cx,cy,cz,page+1 */
  float *page_hash_rgba;
  bool page_hash_tex_ready;
} NgRcWsCtx;

void ng_rc_ws_init(NgRcWsCtx *ws);
void ng_rc_ws_shutdown(NgRcWsCtx *ws);
/** Ensure prim + bvh + chunk page GPU/CPU scratch. */
bool ng_rc_ws_ensure(NgRcWsCtx *ws, int quality);
uint32_t ng_rc_ws_scene_hash(void);
/** Fill NgRcWsPrim from graph inst (pose + materials). false if skip. */
bool ng_rc_ws_inst_prim(int i, NgRcWsPrim *out);
/** Rebuild analytic SDF prims; upload tex_prim; rebuild BVH. */
void ng_rc_ws_rebuild_prims(NgRcWsCtx *ws);
/** Pack CPU BVH → tex_bvh (scene dirty only). */
void ng_rc_ws_upload_bvh(NgRcWsCtx *ws);
/** Upload inst→prim map (scene dirty only). */
void ng_rc_ws_upload_inst_prim(NgRcWsCtx *ws);
/**
 * CPU BVH frustum traverse; fills inst bitset + cull_prim_vis.
 * @return visible inst count.
 */
int ng_rc_ws_cull_traverse(NgRcWsCtx *ws, const Vector4 planes[6], int *vis_inst, int vis_cap);
/** Pack CPU prim vis → tex_prim_vis (R=255 visible). */
void ng_rc_ws_upload_prim_vis(NgRcWsCtx *ws, Texture2D tex);
/** True if inst passed last CPU cull traverse (default true if stale). */
bool ng_rc_ws_inst_visible(const NgRcWsCtx *ws, int inst_i);
/** Chunk cube extent in meters (N * CELL). */
float ng_rc_ws_chunk_extent(void);
/** World-fixed chunk index containing p. */
void ng_rc_ws_world_to_chunk(const float p[3], int32_t *cx, int32_t *cy, int32_t *cz);
/** Perfect hash h = ix + N*(iy + N*iz) in [0, N³). */
int ng_rc_ws_chunk_h(int ix, int iy, int iz);
/** Decode h → local ix,iy,iz. */
void ng_rc_ws_h_to_ijk(int h, int *ix, int *iy, int *iz);
/** World AABB of chunk (cx,cy,cz). */
void ng_rc_ws_chunk_aabb(int32_t cx, int32_t cy, int32_t cz, float bmin[3], float bmax[3]);
/** Cell center from chunk + local h. */
void ng_rc_ws_cell_center(int32_t cx, int32_t cy, int32_t cz, int h, float out[3]);
/**
 * Visible chunks from vis-prim AABBs, cap VIS_MAX; then page_bind.
 */
void ng_rc_ws_chunk_cull(NgRcWsCtx *ws, const Vector4 planes[6]);
/** Bind vis chunks to GPU pages (keep map; LRU-evict non-vis when full). */
void ng_rc_ws_page_bind(NgRcWsCtx *ws);
/** Mark every occupied page dirty (scene change). */
void ng_rc_ws_pages_mark_dirty(NgRcWsCtx *ws);

#endif
// agent: grok-4.6 | 2026-08-21 | drop sparse grid probe API | db96aa
