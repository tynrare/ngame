<!-- agent: composer-2.5 | 2026-08-10 | Phase 6 sparse geo HRC later | f4f91d -->
# Radiance Cascades (3D) — North Star

Goal: **dynamic, deterministic GI** (WebGL2/GLES3). Quality = **cost only**.
Product path = **world-space Radiance Cascades**.  
Compose: `Direct + albedo * (ws·WS_irr) * gi_strength + glow`.

## What ships (aligned with code)

| Piece | Implementation |
|-------|----------------|
| Gbuffer | albedo / normal / glow / depth (`RGB=UVW`, `A=1`) |
| Vox | CPU AABB stamp → `tex_vox` slice atlas (`render_rc_ws.c`) |
| Fill | Dir-packed interval march (`rc_ws_fill.fs`) → atlas `W=N·dirs`, `H=N²` |
| Merge | Same-texel T-merge \(I_n+(1-a_n)I_f\) (`rc_ws_merge.fs`) |
| SH | L1 encode per probe (`rc_ws_sh_encode.fs`) → atlas `W=N·4`, `H=N²` |
| Resolve | Soft-nearest SH × surface **N** → screen irr (`rc_ws_resolve.fs`) |
| Compose | Sample screen WS irr; `ss_weight=0` (SS tick skipped) |
| Quality | Scales **probe_n / dirs / cascades / steps** only |

**Packing:** cascade texel `(px + d·N, py + pz·N)` = `(RGB, α)`.  
**SH bands:** `(px + b·N, …)` for `b ∈ {L0, L1x, L1y, L1z}`.  
**Resolve:** flat cell interiors; blend only near faces (`SOFT_BAND`). Eval `max(0, L0 + L1·N)`.

## Pipeline

```
gbuffer
→ voxelize → tex_vox
→ per cascade: dir-packed fill
→ T-merge far → near
→ L1 SH encode
→ soft-nearest SH×N → screen irr
→ compose (+ glow on surfaces)
```

Key files: `src/client/render.c` (GPU tick), `src/client/render_rc_ws.c` (vox),  
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
| **6** | Global geometry + sparse/adaptive voxels & probes | **next** |
| — | Bloom halo from gbuf glow | **later** (polish; not GI) |
| — | Holographic HRC | **optional later** (quality; not required for scale) |

### Phase 6 — intent

Break the fixed dense brick (CPU `32³` vox + full `N³` probes in a small AABB). Same RC model (fill → merge → SH → resolve); change **geometry backend** and **where** voxels/probes exist.

| Track | Work |
|-------|------|
| **A Geometry** | GPU SDF / SVO / clipmap; raymarch from fill; frustum-only rebuild; octree or clipmap density vs distance |
| **B Probes** | Sparse or camera clipmap probes (hashmap or rings); amortize updates; optional screen-area-constant spacing |

Suggested order: frustum GPU vox → SDF/SVO march in fill → probe clipmap → hashmap sparse.

**Not Phase 6:** Holographic HRC (different cascade geometry for hard shadows; memory-heavy in 3D — see wiki). Sparse RC ≠ HRC.

## Dropped / deferred

Kept out of the product path (idle code or not scheduled):

| Item | Status | Notes |
|------|--------|-------|
| SS bilinear-fix / amortize / miss←WS | **shelved** | `ss_weight=0`; ghosts when on |
| Per-pixel dir loop resolve (N·ω × dirs) | **dropped** | Stars + bad cost; replaced by SH |
| Full 8-tap trilinear of probe irr | **dropped** | Lattice hotspots / “star points” |
| Hard-nearest only | **dropped** | Crisp voxel chunks |
| Wiki bilinear-fix WS merge | **deferred** | Merge is 1:1 texel T-merge only |
| Screen butter on resolved irr | **deferred** | Shader exists; not in tick (no temp RT) |
| Glow as SS emitters | **rejected** | 4-dir ghosts |
| XZ billboard / flatland GI | **rejected** | Debug only |
| L2+ SH / H-basis | **deferred** | L1 is enough for now |
| Bloom | **deferred** | After GI; compose already shows surface glow |
| **Holographic HRC** | **optional later** | Hard shadows / volumetrics; not a substitute for sparse world storage ([wiki](https://radiance.wiki/variants/holographic-rc), [arXiv:2505.02041](https://arxiv.org/abs/2505.02041)) |

## Anti-patterns

- Treating dir-averaged RGB volume as “true RC”
- 2D-atlas hardware bilinear on Y+Z·N packing (shears)
- Investing in SS before WS looks right
- Calling Phase 6 “HRC” when the work is sparse/geo LOD

## References

- https://radiance.wiki/ · direction-first · bilinear-fix · [Holographic RC](https://radiance.wiki/variants/holographic-rc)  
- https://jason.today/rc · https://mini.gmshaders.com/p/radiance-cascades  
- Sparse 3D RC (Sannikov): hashmap / screen-visible probes; SDF or HW RT backend  
- Split Radiance Cascades: [arXiv:2607.20384](https://arxiv.org/abs/2607.20384)  
- https://m4xc.dev/articles/fundamental-rc/ · arXiv:2408.14425 · arXiv:2505.02041  

<!-- agent: composer-2.5 | 2026-08-10 | Phase 6 sparse geo HRC later | f4f91d -->
