<!-- agent: composer-2.5 | 2026-08-12 | docs incremental GPU residency | d72aa7 -->
<!-- agent: composer-2.5 | 2026-08-12 | docs lazy stochastic rebalance | 243b1b -->
<!-- agent: grok-4.6 | 2026-08-12 | even split steal occupancy log | 7823e1 -->
<!-- agent: grok-4.6 | 2026-08-12 | docs lazy persistent cover | 9f683e -->
<!-- agent: grok-4.6 | 2026-08-12 | docs CPU oracle smoke | 91be48 -->
<!-- agent: grok-4.6 | 2026-08-12 | docs persist cover relax | 3c9613 -->
<!-- agent: composer-2.5 | 2026-08-13 | docs BVH scale cull 2048 | c6e2a9 -->
<!-- agent: composer-2.5 | 2026-08-13 | docs CPU BVH cull upload | 655ace -->
# Radiance Cascades (3D) — North Star

Goal: **dynamic, deterministic GI** (WebGL2/GLES3). Quality = **cost only**.
Product path = **world-space Radiance Cascades** on a **surface spatial hash**.

**Performance contract:** probe work ∝ resident slots. Placement from culled meshes, not screen pixels.

**Coverage invariant:** probes cover **culled-visible surfaces** only. Absolute world keys `(lod, ix, iy, iz)` — static geometry keeps stable slots; dynamics rekey partially. No look-at clip volume.

Digests: [`docs/article_rc_gpu_probes.md`](article_rc_gpu_probes.md).

## What ships / target

| Piece | Target | Notes |
|-------|--------|-------|
| Geometry | SDF prims + `inst_i`; BVH (parent+depth) on scene dirty | Up to **2048** cullable entities; cold CPU rebuild OK |
| Cull | **CPU BVH frustum traverse** → inst bitset + `UpdateTexture tex_prim_vis` | Single CPU pass; ~16KB vis upload/frame |
| Draw | **CPU inst bitset** → compact batches before instancing | `ng_rc_ws_cull_traverse` + `ng_rc_ws_inst_visible` |
| Probes | **Persistent GPU residency**: union → release → cover → unmet → relax → cover → split → steal excess | Persist keys; cover-first; CPU oracle `ng_rc_ws_probe_smoke`; MCP `probe_snapshot` |
| Gbuf | After cull + probes | Depth for compose/debug, not placement |
| Debug | `culling` / `probes` / `probes-lod` | `culling` samples `tex_prim_vis`; full gbuf (no batch filter) |
| Compose | Ambient + direct | GI fill offline |

**Gateway:** `src/client/render_rc_ws.c` (`Flow id: rc-ws`).

## Pipeline (hot path)

```
cold: scene dirty? → prims (≤2048) + BVH + tex_inst_prim; GPU clear probe RTs
→ 1) CPU BVH cull: frustum traverse → inst bitset + upload tex_prim_vis
→ 2) probes (GPU only): vis-union AABB → release (outside/empty) →
     cover ≤K (shell; descendants occupy) → split ≤K if headroom (nk≥1) →
     collapse-relax only if unmet at budget → steal excess → stats → meta + hash
CPU oracle: ng_rc_ws_probe_lazy_tick + ./build/ng_rc_ws_probe_smoke (tests/rc_ws)
Persist keys; no view-move cell shuffle.
MCP: probe_snapshot → used/free/lod hist (gateway 27101+)
→ 3) gbuf: CPU batch filter (shipping); debug culling draws all + tex_prim_vis viz
→ 4) swap vis prev←curr
→ compose / debug (sample hash + depth)
```

## Probe residency

Persistent slot pool. No mesh-owner. Release via **vis-union AABB** + SDF shell empty. Lazy cover (K/frame) of missing **surface** cells; finer slots occupy their coarse octant (no parent refill). Split ≤K when under 50% budget — **including nk=1** (single kept child). Steal finest only if over budget. Log / MCP: `used` / `free` / `headroom` / lod hist. CPU gate: [`tests/rc_ws/README.md`](../tests/rc_ws/README.md).

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
| O(inst×prim) inst vis expand | **deprecated** |
| GPU 3-pass `rc_ws_cull` + inst-vis readback | **deprecated** (CPU traverse + upload) |
| VS `ng_inst_cull` / gbuf `tex_inst_vis` kill | **deprecated** |
| CPU frustum duplicate feeding draw via vis upload | deprecated |
| GPU BVH cull as draw authority | **deprecated** |
| VS draw cull as primary path | deprecated (CPU batch filter) |
| Hot-path `LoadImageFromTexture` vis/keep/split | deprecated |
| Split gate CPU readback (`rt_probe_unmet` + slots scan) | **removed** (GPU `allow_split` in `.b`; gen FS gates) |
| AABB-flood Gen0 wipe every tick | **removed** (incremental fill) |
| Full-pool gen replace / early-index budget monopoly | **fixed** (even split-merge) |
| CPU SDF apply / CPU vis lists for probes | **deprecated** |
| Per-frame probe wipe | **removed** (persistent + cold clear) |
| Screen-pixel / depth-seed probe placement | rejected |
| Hot-path CPU freelist / hash pack | **rejected** |

## Phases

| Phase | State |
|------:|--------|
| CPU BVH cull + batch draw | **shipping** |
| GPU incremental probe residency | **shipping** |
| Gbuf-last + fill/resolve | **later** (order shipping) |
| B.1–B.6.6 clip path | **deprecated** |

<!-- agent: composer-2.5 | 2026-08-12 | docs incremental GPU residency | d72aa7 -->
<!-- agent: composer-2.5 | 2026-08-12 | docs lazy stochastic rebalance | 243b1b -->
<!-- agent: grok-4.6 | 2026-08-12 | even split steal occupancy log | 7823e1 -->
<!-- agent: grok-4.6 | 2026-08-12 | docs lazy persistent cover | 9f683e -->
<!-- agent: grok-4.6 | 2026-08-12 | docs CPU oracle smoke | 91be48 -->
<!-- agent: grok-4.6 | 2026-08-12 | docs persist cover relax | 3c9613 -->
<!-- agent: composer-2.5 | 2026-08-13 | docs BVH scale cull 2048 | c6e2a9 -->
<!-- agent: composer-2.5 | 2026-08-13 | docs CPU BVH cull upload | 655ace -->
<!-- agent: composer-2.5 | 2026-08-13 | docs GPU split gate no readback | a1c4e2 -->
