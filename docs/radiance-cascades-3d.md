<!-- agent: composer-2.5 | 2026-08-11 | RC surface hash north star rewrite | 8f6fa6 -->
<!-- agent: composer-2.5 | 2026-08-11 | docs octree cover split note | 3195b6 -->
<!-- agent: composer-2.5 | 2026-08-11 | docs fair 75pct equal tiles | 853a87 -->
<!-- agent: composer-2.5 | 2026-08-11 | docs surface-area 50pct fair | 2fad6f -->
# Radiance Cascades (3D) — North Star

Goal: **dynamic, deterministic GI** (WebGL2/GLES3). Quality = **cost only**.
Product path = **world-space Radiance Cascades** on a **surface spatial hash**.

**Performance contract:** probe work ∝ resident slots. Placement from culled meshes, not screen pixels.

**Coverage invariant:** probes cover **culled-visible surfaces** only. Absolute world keys `(lod, ix, iy, iz)` — static geometry keeps stable slots; dynamics rekey partially. No look-at clip volume.

Digests: [`docs/article_rc_gpu_probes.md`](article_rc_gpu_probes.md).

## What ships (foundation)

| Piece | Implementation |
|-------|----------------|
| Geometry | SDF prims; BVH on scene dirty |
| Cull | GPU frustum → `tex_vis` (positive-vertex AABB) |
| Probes | World octree: coarse cover → **50% pool**, tiles ∝ surface area (even density); drop empty octants |
| Debug | `culling` = vis flags; `probes` = LOD + parity tiles |
| Compose | Ambient + direct (GI fill offline) |

**Gateway:** `src/client/render_rc_ws.c` (`Flow id: rc-ws`).

## Pipeline

```
gbuf (world XYZ)
→ scene dirty? prims + BVH
→ GPU frustum cull → vis prim list
→ surface_tick:
     stamp coarse world cells over culled prim AABBs (≤50% pool)
     poorest-surface-density octree split (share ∝ AABB surface area)
     upload hash/meta
→ debug probes shows tiles; compose ambient+direct
```

## Probe split base

World-snapped octree on culled surfaces; **50% of `slot_cap`** distributed by **surface area** (even tile density), not mesh count. Empty octants dropped.

## Deprecated / refactoring archive

Prior Track B work is **not** the product path; kept in tree for reference only:

| Item | Status |
|------|--------|
| Look-at / far clip cube as GI volume | deprecated |
| Dual near/far clip RTs as coverage | deprecated |
| B.6–B.6.5 evict / steal | superseded |
| **B.6.6** always-cover collapse/split over full clip | deprecated (`ng_rc_ws_sparse_tick`) |
| KD/binary free AABB probe tree | deprecated (replaced by world octree) |
| Clip-grid prim association | deprecated (culling uses SDF; probes use hash) |
| Full-clip cover invariant | replaced by culled-surface cover |

## Phases

| Phase | State |
|------:|--------|
| Cull foundation | **shipping** |
| Surface octree cover→split | **shipping** (debug probes) |
| Further leaf refine / fill / resolve | **next** |
| B.1–B.6.6 clip path | **refactor / deprecated** |

<!-- agent: composer-2.5 | 2026-08-11 | RC surface hash north star rewrite | 8f6fa6 -->
<!-- agent: composer-2.5 | 2026-08-11 | docs octree cover split note | 3195b6 -->
<!-- agent: composer-2.5 | 2026-08-11 | docs fair 75pct equal tiles | 853a87 -->
<!-- agent: composer-2.5 | 2026-08-11 | docs surface-area 50pct fair | 2fad6f -->
