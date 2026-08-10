<!-- agent: composer-2.5 | 2026-08-10 | doc target-center clip volume | 65dfe9 -->
# Radiance Cascades (3D) — North Star

Goal: **dynamic, deterministic GI** (WebGL2/GLES3). Quality = **cost only**.
Product path = **world-space Radiance Cascades**.  
Compose: `Direct + albedo * (ws·WS_irr) * gi_strength + glow`.

## What ships (aligned with code)

| Piece | Implementation |
|-------|----------------|
| Gbuffer | albedo / normal / glow / depth (`RGB=UVW`, `A=1`) |
| Vox | **Look-at–centered** fixed cube + CPU AABB stamp → `tex_vox`; GPU stamp deferred |
| Fill | Dir-packed interval march (`rc_ws_fill.fs`) → atlas `W=N·dirs`, `H=N²` |
| Merge | Same-texel T-merge \(I_n+(1-a_n)I_f\) (`rc_ws_merge.fs`) |
| SH | L1 encode per probe (`rc_ws_sh_encode.fs`) → atlas `W=N·4`, `H=N²` |
| Resolve | Soft-nearest SH × surface **N** → screen irr (`rc_ws_resolve.fs`) |
| Compose | Sample screen WS irr; `ss_weight=0` (SS tick skipped) |
| Quality | Scales **probe_n / dirs / cascades / steps** only |

**Vox volume:** fixed-cell cube (`CELL=0.4`, extent=`VOX_RES·CELL`) centered on **camera target**, snapped to the world grid (+ hysteresis). Orbit (fixed target) keeps the brick locked; do **not** center on frustum AABB (far corners swing the brick off-axis).  
**Packing:** cascade texel `(px + d·N, py + pz·N)` = `(RGB, α)`.  
**SH bands:** `(px + b·N, …)` for `b ∈ {L0, L1x, L1y, L1z}`.  
**Resolve:** flat cell interiors; blend only near faces (`SOFT_BAND`). Eval `max(0, L0 + L1·N)`.

## Pipeline

```
look-at clip origin → gbuffer (UVW)
→ CPU stamp tex_vox
→ per cascade: dir-packed fill
→ T-merge far → near
→ L1 SH encode
→ soft-nearest SH×N → screen irr
→ compose (+ glow on surfaces)
```
Key files: `src/client/render.c` (GPU tick), `src/client/render_rc_ws.c` (inst AABB / quality),  
`res/shaders/rc_ws_{fill,merge,sh_encode,resolve,view}.fs`, `rc_compose.fs`.

## Phases

| Phase | Goal | State |
|------:|------|--------|
| 0–1 | Scene + gbuffer | **done** |
| 2–3 | SS RC shell | **shelved** (code idle) |
| 2b | XZ atlas experiments | **debug only** |
| **4** | WS vox + volume shell | **done** |
| **5a/b** | Dir-packed fill + T-merge | **done** |
| **5c** | L1 SH + soft-nearest resolve | **done** |
| **6.1** | Look-at clip volume + CPU vox stamp | **done** |
| **6.x** | SDF/SVO march; probe clipmap / sparse | **next** |
| — | Bloom halo from gbuf glow | **later** (polish; not GI) |
| — | Holographic HRC | **optional later** (quality; not required for scale) |

### Phase 6 — intent

Break the fixed dense brick. Same RC model (fill → merge → SH → resolve); change **geometry backend** and **where** voxels/probes exist.

| Track | Work | State |
|-------|------|--------|
| **A Geometry** | Look-at / clipmap volume + CPU stamp | **6.1 done** |
| **A Geometry** | SDF / SVO raymarch from fill; octree density | **next** |
| **B Probes** | Sparse or camera clipmap probes; amortize | **later** |

**Not Phase 6:** Holographic HRC (see deferred). Sparse RC ≠ HRC.

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
| **Holographic HRC** | **optional later** | [wiki](https://radiance.wiki/variants/holographic-rc) · [arXiv:2505.02041](https://arxiv.org/abs/2505.02041) |

## Anti-patterns

- Treating dir-averaged RGB volume as “true RC”
- 2D-atlas hardware bilinear on Y+Z·N packing (shears)
- Investing in SS before WS looks right
- Calling Phase 6 “HRC” when the work is sparse/geo LOD
- Centering the clip cube on a long frustum AABB (slides off-axis on orbit)

## References

- https://radiance.wiki/ · direction-first · bilinear-fix · [Holographic RC](https://radiance.wiki/variants/holographic-rc)  
- https://jason.today/rc · https://mini.gmshaders.com/p/radiance-cascades  
- Sparse 3D RC (Sannikov): hashmap / screen-visible probes; SDF or HW RT backend  
- Split Radiance Cascades: [arXiv:2607.20384](https://arxiv.org/abs/2607.20384)  
- https://m4xc.dev/articles/fundamental-rc/ · arXiv:2408.14425 · arXiv:2505.02041  

<!-- agent: composer-2.5 | 2026-08-10 | doc target-center clip volume | 65dfe9 -->
