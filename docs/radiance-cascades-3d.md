<!-- agent: composer-2.5 | 2026-08-12 | GPU hot path north star rewrite | 621156 -->
<!-- agent: composer-2.5 | 2026-08-12 | phases mark GPU path shipping | da9e49 -->
# Radiance Cascades (3D) — North Star

Goal: **dynamic, deterministic GI** (WebGL2/GLES3). Quality = **cost only**.
Product path = **world-space Radiance Cascades** on a **surface spatial hash**.

**Performance contract:** probe work ∝ resident slots. Placement from culled meshes, not screen pixels.

**Coverage invariant:** probes cover **culled-visible surfaces** only. Absolute world keys `(lod, ix, iy, iz)` — static geometry keeps stable slots; dynamics rekey partially. No look-at clip volume.

Digests: [`docs/article_rc_gpu_probes.md`](article_rc_gpu_probes.md).

## What ships / target

| Piece | Target | Notes |
|-------|--------|-------|
| Geometry | SDF prims + `inst_i`; BVH on scene dirty | Cold CPU rebuild OK |
| Cull | GPU frustum → `tex_vis` (sole frustum authority) | Tiny `tex_vis` readback feeds draw filter |
| Draw | CPU drops culled meshes / active prims | No CPU frustum retest |
| Probes | GPU cover → gen waves → `tex_hash` / `tex_meta` | CPU pack after GPU shell keep; `surface_tick` retired from hot path |
| Gbuf | After cull + filter (+ probes) | Depth for compose/debug, not placement |
| Debug | `culling` = vis; `probes` / `probes-lod` = hash tiles | `rt_probe_id` is viz/deferred aid |
| Compose | Ambient + direct | GI fill offline |

**Gateway:** `src/client/render_rc_ws.c` (`Flow id: rc-ws`).

## Pipeline (hot path)

```
cold: scene dirty? → prims + BVH + inst_i
→ 1) GPU frustum cull → tex_vis
→ 2) CPU drop culled meshes / active prim set (from tex_vis readback)
→ 3) GPU probes: coarse cover → gen-complete waves (SDF shell; ≤50% slot_cap) → tex_hash
→ 4) gbuf (albedo/normal/glow/depth) from visible batches only
→ compose / debug (sample hash + depth)
```

## Probe split base

World-snapped octree on culled surfaces; **50% of `slot_cap`** distributed by **surface area** (even density), not mesh count. Empty / air / interior octants dropped.

## Deprecated / refactoring archive

| Item | Status |
|------|--------|
| Look-at / far clip cube as GI volume | deprecated |
| Dual near/far clip RTs as coverage | deprecated |
| B.6–B.6.5 evict / steal | superseded |
| **B.6.6** always-cover collapse/split over full clip | deprecated (`ng_rc_ws_sparse_tick`) |
| KD/binary free AABB probe tree | deprecated (world octree) |
| Clip-grid prim association | deprecated |
| Full-clip cover invariant | replaced by culled-surface cover |
| CPU frustum duplicate feeding probes | deprecated (GPU cull sole) |
| Screen-pixel / depth-seed probe placement | rejected |

## Phases

| Phase | State |
|------:|--------|
| Cull sole + CPU draw drop (`inst_i`, `tex_vis` readback) | **shipping** |
| GPU Gen0 coarse cover → hash | **shipping** |
| GPU gen waves + shell keep + budget | **shipping** |
| Gbuf-last reorder + fill/resolve | **shipping** (fill later) |
| CPU `surface_tick` hot path | **retired** (API retained) |
| B.1–B.6.6 clip path | **deprecated** |

<!-- agent: composer-2.5 | 2026-08-12 | GPU hot path north star rewrite | 621156 -->
<!-- agent: composer-2.5 | 2026-08-12 | phases mark GPU path shipping | da9e49 -->
