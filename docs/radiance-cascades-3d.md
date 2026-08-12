<!-- agent: composer-2.5 | 2026-08-12 | docs GPU copies surface split | 702dd1 -->
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
| Cull | GPU frustum → `tex_vis` + expand `tex_inst_vis` | No hot-path readback / no CPU vis upload |
| Draw | VS samples `tex_inst_vis` (kill culled) | CPU builds all matrices; material-map bind |
| Probes | GPU cover → keep → **split-merge** waves → meta/hash | Copies CPU `surface_tick` split; fingerprint skip |
| Gbuf | After cull + probes | Depth for compose/debug, not placement |
| Debug | `culling` / `probes` / `probes-lod` | `rt_probe_id` viz aid |
| Compose | Ambient + direct | GI fill offline |

**Gateway:** `src/client/render_rc_ws.c` (`Flow id: rc-ws`).

## Pipeline (hot path)

```
cold: scene dirty? → prims + BVH + inst_i
→ 1) GPU frustum cull → tex_vis_curr → expand tex_inst_vis
→ 2) probes: Gen0 shell cover → gen waves (AABB+shell kids, split-merge retain parents) → meta + open-address hash; fingerprint skip
→ 3) gbuf: VS cull via tex_inst_vis (no CPU filter)
→ 4) swap vis prev←curr
→ compose / debug (sample hash + depth)
```

## Probe split base

World-snapped octree on **culled-visible** surfaces; **50% of `slot_cap`**. Empty / air / interior octants dropped. GPU gen waves **retain unsplit parents** (no full-pool child-only replace). Slot/meta/hash RTs persist; rebuild on view fingerprint miss.

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
| CPU `UpdateTexture` of prim/inst vis | deprecated (GPU cull + expand) |
| Hot-path `LoadImageFromTexture` vis/keep | deprecated |
| CPU SDF apply / CPU vis lists for probes | **deprecated** (GPU cover/keep/split-merge) |
| Full-pool gen replace (children-only wipe) | **fixed** (split-merge) |
| Per-frame probe wipe | deprecated (fingerprint + incremental) |
| Screen-pixel / depth-seed probe placement | rejected |

## Phases

| Phase | State |
|------:|--------|
| GPU cull + VS draw cull (no vis readback) | **shipping** |
| GPU probes copy `surface_tick` split (split-merge + open-address hash) | **shipping** |
| Gbuf-last + fill/resolve | **later** (order shipping) |
| B.1–B.6.6 clip path | **deprecated** |

<!-- agent: composer-2.5 | 2026-08-12 | docs GPU copies surface split | 702dd1 -->
