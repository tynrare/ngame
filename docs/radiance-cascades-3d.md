<!-- agent: composer-2.5 | 2026-08-12 | docs incremental GPU residency | d72aa7 -->
<!-- agent: composer-2.5 | 2026-08-12 | docs lazy stochastic rebalance | 243b1b -->
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
| Probes | **Persistent GPU residency**: union → release → compact → cover → lazy K stochastic split/steal → meta/hash | Occupancy log: used/freeable; refine over frames |
| Gbuf | After cull + probes | Depth for compose/debug, not placement |
| Debug | `culling` / `probes` / `probes-lod` | `rt_probe_id` viz aid |
| Compose | Ambient + direct | GI fill offline |

**Gateway:** `src/client/render_rc_ws.c` (`Flow id: rc-ws`).

## Pipeline (hot path)

```
cold: scene dirty? → prims + BVH + inst_i; GPU clear probe RTs
→ 1) GPU frustum cull → tex_vis_curr → expand tex_inst_vis
→ 2) probes (GPU only): vis-union AABB → release (outside/empty) → compact →
     cover free slots → lazy stochastic split (K=16/frame) → steal densest (K=16) →
     stats → meta + open-address hash
→ 3) gbuf: VS cull via tex_inst_vis (no CPU filter)
→ 4) swap vis prev←curr
→ compose / debug (sample hash + depth)
```

## Probe residency

Persistent slot pool. No mesh-owner. Release via **vis-union AABB** + SDF shell empty. Cover gaps into **free** slots. **Lazy stochastic** split/steal (K=16 per frame) rebalances coarse vs dense over time — no exact balance. Log on view change: `used` / `freeable` / `budget`. Hash/meta on GPU from slots.

## Deprecated / refactoring archive

| Item | Status |
|------|--------|
| Look-at / far clip cube as GI volume | deprecated |
| Dual near/far clip RTs as coverage | deprecated |
| B.6–B.6.5 clip evict / steal | superseded by GPU densest steal |
| **B.6.6** always-cover collapse/split over full clip | deprecated (`ng_rc_ws_sparse_tick`) |
| KD/binary free AABB probe tree | deprecated (world octree) |
| Clip-grid prim association | deprecated |
| Full-clip cover invariant | replaced by culled-surface cover |
| CPU frustum duplicate feeding draw | deprecated (VS cull) |
| CPU `UpdateTexture` of prim/inst vis | deprecated (GPU cull + expand) |
| Hot-path `LoadImageFromTexture` vis/keep | deprecated |
| AABB-flood Gen0 wipe every tick | **removed** (incremental fill) |
| Full-pool gen replace / early-index budget monopoly | **fixed** (even split-merge) |
| CPU SDF apply / CPU vis lists for probes | **deprecated** |
| Per-frame probe wipe | **removed** (persistent + cold clear) |
| Screen-pixel / depth-seed probe placement | rejected |
| Hot-path CPU freelist / hash pack | **rejected** |

## Phases

| Phase | State |
|------:|--------|
| GPU cull + VS draw cull (no vis readback) | **shipping** |
| GPU incremental probe residency | **shipping** |
| Gbuf-last + fill/resolve | **later** (order shipping) |
| B.1–B.6.6 clip path | **deprecated** |

<!-- agent: composer-2.5 | 2026-08-12 | docs incremental GPU residency | d72aa7 -->
<!-- agent: composer-2.5 | 2026-08-12 | docs lazy stochastic rebalance | 243b1b -->
