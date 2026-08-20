// agent: composer-2.5 | 2026-08-11 | B62 GPU prio API header | 379f19
// agent: composer-2.5 | 2026-08-11 | B63 cover split API header | 5da9fc
// agent: composer-2.5 | 2026-08-11 | B64 prio GPU API header | 5d3edb
// agent: composer-2.5 | 2026-08-11 | B65 frustum view API header | 7b5d3a
// agent: composer-2.5 | 2026-08-11 | B66 always-cover API header | f86d08
// agent: composer-2.5 | 2026-08-11 | GI offline BVH cull foundation | 5c2aa5
// agent: composer-2.5 | 2026-08-11 | surface tick API header | d996d7
// agent: composer-2.5 | 2026-08-11 | LOD_SOFT_MAX for root cover | 92f417
// agent: composer-2.5 | 2026-08-12 | inst_i on NgRcWsPrim | 8dfbc3
// agent: composer-2.5 | 2026-08-12 | GPU probe tick API | af2c49
// agent: composer-2.5 | 2026-08-12 | probe API incremental | 134400
// agent: grok-4.6 | 2026-08-12 | lazy probe CPU oracle API | 3cc2dd
// agent: composer-2.5 | 2026-08-13 | BVH scale cull 2048 caps | f8b2d1
#ifndef NG_RENDER_RC_WS_H
#define NG_RENDER_RC_WS_H

#include <raylib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NG_RC_WS_PROBE_MAX 16
#define NG_RC_WS_GI_NEAR 0.25f
#define NG_RC_WS_GI_FAR 28.0f
/** Finest world meters (LOD 0). cell(L) = CELL * 2^L. */
#define NG_RC_WS_CELL 0.4f
/** Legacy enter-table / offline GI LOD count (0 = finest … LOD_MAX-1). */
#define NG_RC_WS_LOD_MAX 8
/** Soft ceiling for cell_size / surface root (uint8 slot lod; 0.4*2^24 ≈ huge). */
#define NG_RC_WS_LOD_SOFT_MAX 24
/** Clip cube extent = CELL × this (fixed; never cam/frustum stretch). */
#define NG_RC_WS_VOX_RES 32
/** Max cullable analytic SDF prims (matches scene inst cap). */
#define NG_RC_WS_PRIM_MAX 2048
/** Graph inst cap mirror for cull / inst_prim map. */
#define NG_RC_WS_INST_MAX 2048
// agent: composer-2.5 | 2026-08-13 | CPU BVH cull traverse API | 93b212
#define NG_RC_WS_AABB_PAD 0.05f
#define NG_RC_WS_CULL_STACK 64
/** tex_prim_vis upload bytes (PRIM_MAX × 2 rows RGBA8). */
#define NG_RC_WS_PRIM_VIS_BYTES (NG_RC_WS_PRIM_MAX * 2 * 4)
/** Inst-vis threshold (matches probe FS 0.5). */
#define NG_RC_WS_CULL_VIS_THRESH 128
/** tex_prim columns: center+type, half, quat, emit+rough, albedo+metal, lit+inst, leaf+pad. */
#define NG_RC_WS_PRIM_COLS 7
/** Uniform candidate grid resolution per clip axis. */
#define NG_RC_WS_GRID_RES 8
/** Prim index slots per grid cell (RGBA8 channels). 255 = empty. */
#define NG_RC_WS_GRID_SLOT 4
#define NG_RC_WS_GRID_CELLS (NG_RC_WS_GRID_RES * NG_RC_WS_GRID_RES * NG_RC_WS_GRID_RES)
#define NG_RC_WS_GRID_BYTES (NG_RC_WS_GRID_CELLS * NG_RC_WS_GRID_SLOT)
/** Sparse probe slot pool (quality-scaled). */
#define NG_RC_WS_SLOT_MAX 512
/** Open-address hash capacity (power-of-two). */
#define NG_RC_WS_HASH_MAX 1024
/** Hash packs slot+1 + lod*1000 into R. */
#define NG_RC_WS_HASH_LOD_STRIDE 1000
/** Flat binary BVH capacity (~2N-1 for PRIM_MAX leaves). */
#define NG_RC_WS_BVH_MAX 4096
/** tex_bvh columns: bmin+left, bmax+right, prim+parent, depth+pad. */
#define NG_RC_WS_BVH_COLS 4
#define NG_RC_WS_BVH_FLOATS (NG_RC_WS_BVH_MAX * NG_RC_WS_BVH_COLS * 4)
#define NG_RC_WS_INST_PRIM_FLOATS (NG_RC_WS_INST_MAX * 4)
#define NG_RC_WS_CULL_VIS_WORDS ((NG_RC_WS_INST_MAX + 31) / 32)
/** Top-K / per-tick structure ops (amortize remarch). */
#define NG_RC_WS_PRIO_K 16
/** Max GPU shell work items per batch (Gen0 / wave). */
#define NG_RC_WS_PROBE_WORK_MAX 2048
/** Foundation: GI fill offline; GPU cull + GPU probe cover/waves. */
#define NG_RC_WS_GI_OFFLINE 1

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

typedef struct NgRcWsSlot {
  int32_t ix;
  int32_t iy;
  int32_t iz;
  uint8_t lod; /* 0..LOD_MAX-1 */
  uint8_t used;
  uint8_t dirty; /* 1 = needs cascade fill this frame */
  uint8_t pad; /* 1 = face-pad (retain while neighbor seed lives) */
} NgRcWsSlot;

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
  bool grid_tex_ready;
  bool bvh_tex_ready;
  bool inst_prim_tex_ready;
  bool sparse_tex_ready;
  bool prio_tex_ready;
  Texture2D tex_prim; /* PRIM_COLS × PRIM_MAX RGBA32F */
  Texture2D tex_grid; /* GRID_RES × (GRID_RES²) RGBA8, 4 slots/cell */
  Texture2D tex_bvh; /* BVH_COLS × BVH_MAX RGBA32F */
  Texture2D tex_inst_prim; /* 1 × INST_MAX prim+1 per inst (0 = none) */
  Texture2D tex_meta; /* 1 × slot_cap RGBA32F center+occ */
  Texture2D tex_hash; /* hash_size × 1 RGBA32F slot+lod, xyz */
  Texture2D tex_prio; /* slot_cap × 1 RGBA32F promote/relax/id/flags */
  float *prim_rgba;
  unsigned char *grid_rgba;
  float *bvh_rgba;
  float *inst_prim_rgba;
  float *meta_rgba;
  float *hash_rgba;
  float *prio_rgba; /* cached scores; mirrors rc_ws_prio.fs */
  NgRcWsPrim prims[NG_RC_WS_PRIM_MAX];
  int16_t inst_prim[NG_RC_WS_INST_MAX]; /* prim row or -1 */
  NgRcWsSlot slots[NG_RC_WS_SLOT_MAX];
  int32_t hash_tab[NG_RC_WS_HASH_MAX];
  NgRcWsBvhNode bvh[NG_RC_WS_BVH_MAX];
  int bvh_root; /* -1 empty */
  int bvh_count;
  int bvh_max_depth;
  int prim_count;
  int slot_cap;
  int slot_count;
  int hash_size;
  float origin[3];
  float size[3];
  float eye[3]; /* cam.position */
  float forward[3]; /* cam look direction (normalized) */
  float tan_half_fov; /* vertical */
  float aspect; /* w/h */
  int probe_n; /* legacy name: slot_cap mirror for callers */
  int dirs;
  int cascades;
  int steps;
  uint32_t scene_hash;
  int frames_since_vox;
  uint32_t tick_fp; /* idle early-out: eye+forward+clip+prim fingerprint */
  uint32_t cull_vis_bits[NG_RC_WS_CULL_VIS_WORDS];
  int cull_vis_n;
  bool cull_valid;
  // agent: composer-2.5 | 2026-08-13 | CPU BVH cull traverse API | 93b212
  uint8_t cull_prim_vis[NG_RC_WS_PRIM_MAX];
  unsigned char *prim_vis_rgba;
} NgRcWsCtx;

/** cell(L) = CELL * 2^L */
float ng_rc_ws_cell_size(int lod);
/** Distance→LOD (0 finest) without hysteresis. */
int ng_rc_ws_lod_for_dist(float dist);
/** Outer enter radius for LOD (meters from look-at). */
float ng_rc_ws_lod_enter(int lod);

void ng_rc_ws_init(NgRcWsCtx *ws);
void ng_rc_ws_shutdown(NgRcWsCtx *ws);
/** Ensure prim + grid + bvh GPU/CPU scratch. */
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
/** Stamp prim AABB into clip grid; upload tex_grid (after rebuild_prims). */
void ng_rc_ws_rebuild_grid(NgRcWsCtx *ws);
/**
 * CPU BVH frustum traverse; fills inst bitset + cull_prim_vis.
 * @return visible inst count.
 */
int ng_rc_ws_cull_traverse(NgRcWsCtx *ws, const Vector4 planes[6], int *vis_inst, int vis_cap);
/** Pack CPU prim vis → tex_prim_vis (R=255 visible). */
void ng_rc_ws_upload_prim_vis(NgRcWsCtx *ws, Texture2D tex);
/** True if inst passed last CPU cull traverse (default true if stale). */
bool ng_rc_ws_inst_visible(const NgRcWsCtx *ws, int inst_i);

/** Clear all probe slots (GPU probe tick). */
void ng_rc_ws_probe_clear(NgRcWsCtx *ws);
/** Insert world cell if free; returns slot or -1. */
int ng_rc_ws_probe_insert_cell(NgRcWsCtx *ws, uint8_t lod, int32_t ix, int32_t iy, int32_t iz);
/** Used slot count. */
int ng_rc_ws_probe_used(const NgRcWsCtx *ws);
/** Upload slots → tex_meta / tex_hash. */
void ng_rc_ws_probe_upload(NgRcWsCtx *ws);
/**
 * Apply octree split keeping GPU-approved children (bit0..7).
 * @return 1 if parent replaced.
 */
int ng_rc_ws_probe_apply_split_mask(NgRcWsCtx *ws, int si, unsigned keep_mask);
/** Enumerate Gen0 AABB cells for prim into work arrays (no SDF). */
int ng_rc_ws_probe_enum_cover(const NgRcWsCtx *ws, int pi, int lod, int32_t *ix, int32_t *iy,
                              int32_t *iz, int *prim_out, int cap);
/** Enumerate 8 children of leaf si into work arrays. */
int ng_rc_ws_probe_enum_children(const NgRcWsCtx *ws, int si, int32_t *ix, int32_t *iy, int32_t *iz,
                                 int *lod_out, int *parent_out, int *child_out, int cap);
/** True if world cell hits prim SDF shell. */
int ng_rc_ws_probe_cell_keep(const NgRcWsCtx *ws, int pi, int lod, int32_t ix, int32_t iy,
                             int32_t iz);
/** Fingerprint for probe skip: scene + quantized eye/forward. */
uint32_t ng_rc_ws_probe_view_fp(const NgRcWsCtx *ws, const float eye[3], const float forward[3]);
/** Evict used slots that fail shell against any of vis_prims (incremental). */
void ng_rc_ws_probe_evict_unvis(NgRcWsCtx *ws, const int *vis_prims, int vis_n);

/**
 * CPU-only probe pool (no GL textures). For smoke / oracle.
 * @return 1 on success.
 */
int ng_rc_ws_probe_cpu_begin(NgRcWsCtx *ws, int slot_cap);
/**
 * Lazy persistent tick (GPU oracle): release → cover≤K → split≤K (nk≥1) →
 * steal finest if over budget.
 */
void ng_rc_ws_probe_lazy_tick(NgRcWsCtx *ws, const int *vis_prims, int vis_n, uint32_t frame,
                              int cover_k, int split_k);
/** Min used lod, or 99 if empty. */
int ng_rc_ws_probe_lod_min(const NgRcWsCtx *ws);
/** Max used lod, or -1 if empty. */
int ng_rc_ws_probe_lod_max(const NgRcWsCtx *ws);
/** Write used/budget/lod hist into out. */
void ng_rc_ws_probe_hist_text(const NgRcWsCtx *ws, char *out, size_t cap);
/** True if any slot covers octant (lod,ix,iy,iz) as self/ancestor/descendant. */
int ng_rc_ws_probe_octant_occupied(const NgRcWsCtx *ws, int lod, int32_t ix, int32_t iy,
                                   int32_t iz);
/** Count used slots whose cell shell-hits prim pi. */
int ng_rc_ws_probe_slots_on_prim(const NgRcWsCtx *ws, int pi);
/** True if at least one used slot shell-covers prim pi. */
int ng_rc_ws_probe_prim_cover_ok(const NgRcWsCtx *ws, int pi);
/** True if vis set has unmet cover_lod shell cell (not octant-occupied). */
int ng_rc_ws_probe_cover_unmet(const NgRcWsCtx *ws, const int *vis_prims, int vis_n);
/** Stable fingerprint of used keys (persist smoke). */
uint64_t ng_rc_ws_probe_key_fp(const NgRcWsCtx *ws);
// agent: grok-4.6 | 2026-08-12 | cover unmet helper API | 5f4345
// agent: grok-4.6 | 2026-08-12 | persist cover-first relax | 052e50

/**
 * @deprecated transitional — use GPU probe tick in render.c.
 */
void ng_rc_ws_surface_tick(NgRcWsCtx *ws, const int *vis_prims, int vis_n);
/**
 * Deprecated (refactor): B.6.6 full-clip cover + collapse/split.
 */
void ng_rc_ws_sparse_tick(NgRcWsCtx *ws, const float origin[3], const float size[3],
                          const float eye[3]);
/** Set view basis for coarsen/promote scores (call before sparse_tick). */
void ng_rc_ws_set_view(NgRcWsCtx *ws, const float forward[3], float tan_half_fov, float aspect);
/** Fill top-K promote/relax indices from cached prio_rgba (CPU select / GPU list mirror). */
void ng_rc_ws_prio_select(const NgRcWsCtx *ws, int *promote_ids, int *np, int *relax_ids, int *nr,
                          int kmax);
/** Count used slots with dirty flag. */
int ng_rc_ws_dirty_count(const NgRcWsCtx *ws);
/** Mark every used slot dirty (force full sparse refill). */
void ng_rc_ws_sparse_mark_dirty(NgRcWsCtx *ws);
/** Clear dirty flags and refresh tex_meta.a after fill. */
void ng_rc_ws_sparse_clear_dirty(NgRcWsCtx *ws);

// agent: grok-4.6 | 2026-08-12 | cover unmet helper API | 5f4345
#endif
// agent: composer-2.5 | 2026-08-13 | CPU BVH cull traverse API | 93b212
// agent: composer-2.5 | 2026-08-13 | GPU readback inst vis bitset | a8e3f1
// agent: composer-2.5 | 2026-08-13 | BVH scale cull 2048 caps | f8b2d1
