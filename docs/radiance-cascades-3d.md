<!-- agent: composer-2.5 | 2026-08-10 | doc phase 6.2 SDF plan | dc74da -->
# Radiance Cascades (3D) — North Star

Goal: **dynamic, deterministic GI** (WebGL2/GLES3). Quality = **cost only**.
Product path = **world-space Radiance Cascades**.  
Compose: `Direct + albedo * (ws·WS_irr) * gi_strength + glow`.

## What ships (aligned with code)

| Piece | Implementation |
|-------|----------------|
| Gbuffer | albedo / normal / glow / depth (`RGB=UVW`, `A=1`) |
| Clip | Look-at–centered fixed cube (`CELL=0.4`, extent=`VOX_RES·CELL`), world-grid snap + hysteresis |
| Geometry (6.1) | CPU AABB stamp → `tex_vox` (chunky; spheres≈boxes) |
| Fill | Dir-packed interval march (`rc_ws_fill.fs`) → atlas `W=N·dirs`, `H=N²` |
| Merge | Same-texel T-merge \(I_n+(1-a_n)I_f\) (`rc_ws_merge.fs`) |
| SH | L1 encode per probe (`rc_ws_sh_encode.fs`) → atlas `W=N·4`, `H=N²` |
| Resolve | Soft-nearest SH × surface **N** → screen irr (`rc_ws_resolve.fs`) |
| Compose | Sample screen WS irr; `ss_weight=0` (SS tick skipped) |
| Quality | Scales **probe_n / dirs / cascades / steps** only |

**Clip:** orbit with fixed target keeps the brick world-locked. Do **not** center on frustum AABB (far corners swing off-axis).  
**Packing / SH / resolve:** unchanged from Phase 5.  
**Gateway:** `src/client/render_rc_ws.c` (`Flow id: rc-ws`); tick orchestration in `src/client/render.c`.

## Pipeline (current)

```
look-at clip origin → gbuffer (UVW)
→ CPU stamp tex_vox
→ per cascade: dir-packed fill (sample_vox)
→ T-merge far → near
→ L1 SH encode
→ soft-nearest SH×N → screen irr
→ compose
```

## Phases

| Phase | Goal | State |
|------:|------|--------|
| 0–1 | Scene + gbuffer | **done** |
| 2–3 | SS RC shell | **shelved** (code idle) |
| 2b | XZ atlas experiments | **debug only** |
| **4** | WS vox + volume shell | **done** |
| **5a/b** | Dir-packed fill + T-merge | **done** |
| **5c** | L1 SH + soft-nearest resolve | **done** |
| **6.1** | Look-at clip + CPU vox stamp | **done** |
| **6.2** | Analytic SDF fill from described meshes | **next** |
| **6.3** | SVO / spatial index (optional) | **later** |
| **B** | Sparse / clipmap probes | **later** |
| — | Bloom halo from gbuf glow | **later** (polish; not GI) |
| — | Holographic HRC | **optional later** |

### Phase 6 — intent

Same RC model (fill → merge → SH → resolve). Change **geometry backend** (Track A) and later **where probes exist** (Track B).

| Track | Work | State |
|-------|------|--------|
| **A** | Look-at clip + CPU stamp | **6.1 done** |
| **A** | Analytic SDF from mesh/model/entity | **6.2 next** |
| **A** | SVO as empty-skip / probe scaffold | **6.3 later** |
| **B** | Sparse hashmap / clipmap probes | **later** |

**Not Phase 6:** Holographic HRC. Sparse RC ≠ HRC. SVO is not required to start 6.2.

---

## Phase 6.2 — Analytic SDF (expanded plan)

### Why

Meshes are already primitives (`NG_SCENE_MESH_CUBE` / `SPHERE` + extents). Stamp voxelizes AABBs and loses shape. Fill should sphere-trace **described SDFs**. Backend is independent of cascade storage ([sparse RC](https://www.youtube.com/watch?v=TGLAxW0xVDU), [Split RC](https://arxiv.org/abs/2607.20384)).

### Constraints

- WebGL2/GLES3: **fixed-cap UBO or RGBA texture pack** for prims (no compute SSBO dependency).
- Cap ~32–64 (scene graph is small; `NG_SCENE_ASSET_MAX` = 32).
- Merge / SH / resolve / compose **unchanged**.
- Keep look-at clip from 6.1.

### Slices

| Id | Work | Outcome |
|----|------|---------|
| **6.2.0** | Prim contract: `type`, center, half/radius, `lit.rgb`; CPU rebuild list on dirty | Upload path ready |
| **6.2.1** | `rc_ws_fill.fs` sphere-traces `min` SDF instead of `sample_vox` | Round spheres, thin walls |
| **6.2.2** | Lit/emit identity matches stamp intent; floor skip or real `sdBox` | Bleed sources correct |
| **6.2.3** | Overlap cull vs clip; AABB early-out; step budget via quality | Cost stays knobs-only |
| **6.2.4** | Demote dense `tex_vox` off product path (debug optional) | No double geometry |

### SVO (6.3 — not 6.2)

Use later for empty-space skip, large worlds, or **sparse probe keys** (occupied leaves). Optional refine: SVO large step → analytic SDF near leaves. Do not build SVO before SDF fill works.

### First steps (do these next)

1. **6.2.0a** — Define `NgRcWsPrim` + fixed array on `NgRcWsCtx`; `ng_rc_ws_rebuild_prims` from graph (reuse `ng_rc_ws_inst_stamp` fields: center/half/lit + mesh_kind).
2. **6.2.0b** — Upload prims (texture rows or UBO); bind in WS fill; keep vox path until 6.2.1 switches.
3. **6.2.1** — Replace march body with `scene_sdf` + hit `lit`; verify irradiance: spheres round, wall thin, orbit world-locked.

**Pass (6.2):** orbit keeps bleed locked; no voxel stairs; WebGL2-safe upload; quality knobs still dominate cost.

---

## Dropped / deferred

| Item | Status | Notes |
|------|--------|-------|
| SS bilinear-fix / amortize / miss←WS | **shelved** | `ss_weight=0` |
| Per-pixel dir loop resolve | **dropped** | Replaced by SH |
| Full 8-tap trilinear / hard-nearest | **dropped** | Hotspots / voxels |
| Wiki bilinear-fix WS merge | **deferred** | 1:1 texel T-merge |
| Screen butter | **deferred** | Not in tick |
| Glow as SS emitters / XZ flatland GI | **rejected** | |
| L2+ SH | **deferred** | |
| Bloom | **deferred** | |
| Frustum-AABB–centered brick | **rejected** | Far-corner swing; use look-at (6.1) |
| Dense vox as product geometry | **demote in 6.2.4** | Debug only after SDF |
| SVO in 6.2 | **rejected** | Move to 6.3 |
| **Holographic HRC** | **optional later** | [wiki](https://radiance.wiki/variants/holographic-rc) · [arXiv:2505.02041](https://arxiv.org/abs/2505.02041) |

## Anti-patterns

- Treating dir-averaged RGB volume as “true RC”
- 2D-atlas hardware bilinear on Y+Z·N packing (shears)
- Investing in SS before WS looks right
- Calling Phase 6 “HRC” when the work is sparse/geo LOD
- Centering the clip cube on a long frustum AABB (slides off-axis on orbit)
- Building SVO before analytic SDF fill works
- Baking triangle meshes when describe already is box/sphere

## References

- https://radiance.wiki/ · direction-first · bilinear-fix · [Holographic RC](https://radiance.wiki/variants/holographic-rc)  
- https://jason.today/rc · https://mini.gmshaders.com/p/radiance-cascades  
- Sparse 3D RC (Sannikov): hashmap / screen-visible probes; SDF or HW RT backend  
- Split Radiance Cascades: [arXiv:2607.20384](https://arxiv.org/abs/2607.20384)  
- https://m4xc.dev/articles/fundamental-rc/ · arXiv:2408.14425 · arXiv:2505.02041  

<!-- agent: composer-2.5 | 2026-08-10 | doc phase 6.2 SDF plan | dc74da -->
