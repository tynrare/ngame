<!-- agent: composer-2.5 | 2026-08-12 | north star zero readback | 3f68ec -->
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
| Cull | GPU frustum → `tex_vis` + `tex_inst_vis` | No hot-path readback |
| Draw | VS samples `tex_inst_vis` (kill culled) | CPU builds all matrices only |
| Probes | Prev-frame `tex_vis`; fingerprint skip; persistent slots | CPU SDF apply; GPU shell assist |
| Gbuf | After cull + probes | Depth for compose/debug, not placement |
| Debug | `culling` / `probes` / `probes-lod` | `rt_probe_id` viz aid |
| Compose | Ambient + direct | GI fill offline |

**Gateway:** `src/client/render_rc_ws.c` (`Flow id: rc-ws`).

## Pipeline (hot path)

```
cold: scene dirty? → prims + BVH + inst_i
→ 1) GPU frustum cull → tex_vis_curr → expand tex_inst_vis
→ 2) probes from tex_vis_prev (skip if view fingerprint idle; evict/insert)
→ 3) gbuf: VS cull via tex_inst_vis (no CPU filter)
→ 4) swap vis prev←curr
→ compose / debug (sample hash + depth)
```

## Probe split base

World-snapped octree on culled surfaces; **50% of `slot_cap`** distributed by **surface area** (even density), not mesh count. Empty / air / interior octants dropped. Slot pool persists across frames.

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
| CPU frustum duplicate feeding draw | deprecated (VS cull) |
| Hot-path `LoadImageFromTexture` vis/keep | deprecated |
| Per-frame probe wipe | deprecated (fingerprint + incremental) |
| Screen-pixel / depth-seed probe placement | rejected |

## Phases

| Phase | State |
|------:|--------|
| GPU cull + VS draw cull (no vis readback) | **shipping** |
| Prev-vis probes + fingerprint + persistent slots | **shipping** |
| CPU SDF apply (no keep readback) | **shipping** |
| Full GPU meta/hash scatter | **later** |
| Gbuf-last + fill/resolve | **later** (order shipping) |
| B.1–B.6.6 clip path | **deprecated** |

<!-- agent: composer-2.5 | 2026-08-12 | north star zero readback | 3f68ec -->
