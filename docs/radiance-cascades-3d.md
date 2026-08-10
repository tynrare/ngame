<!-- agent: composer-2.5 | 2026-08-10 | doc resolve trilinear note | 5101c2 -->
# Radiance Cascades (3D) — North Star

Goal: **dynamic, deterministic GI** (WebGL2/GLES3). Quality = **cost only**.
Product path = **world-space Radiance Cascades**.  
Compose: `Direct + albedo * (ws·WS_irr) * gi_strength + glow`.

## What ships (aligned with code)

| Piece | Implementation |
|-------|----------------|
| Gbuffer | albedo / normal / glow / depth (`RGB=UVW`, `A=1`) |
| Clip | Look-at–centered fixed cube (`CELL=0.4`, extent=`VOX_RES·CELL`), world-grid snap + hysteresis |
| Geometry (**6.2**) | Analytic SDF prims (`tex_prim`): every mesh entity (incl. floor); box/sphere + quat + lit/PBR |
| Fill | Dir-packed SDF interval march (`rc_ws_fill.fs`) → atlas `W=N·dirs`, `H=N²` |
| Merge | Same-texel T-merge \(I_n+(1-a_n)I_f\) (`rc_ws_merge.fs`) |
| SH | L1 encode per probe (`rc_ws_sh_encode.fs`) → atlas `W=N·4`, `H=N²` |
| Resolve | **Trilinear** L1 SH × **N** via `texelFetch` (`rc_ws_resolve.fs`) |
| Compose | Sample screen WS irr; `ss_weight=0` (SS tick skipped) |
| Quality | Scales **probe_n / dirs / cascades / steps** only |

**Clip:** orbit with fixed target keeps the brick world-locked. Do **not** center on frustum AABB (far corners swing off-axis).  
**Packing / SH:** cascade `(d·N+ix, iy+iz·N)`; SH bands along X; sample with **texelFetch** (no Y+Z shear from bilinear).  
**Resolve:** 8-corner trilinear of SH coeffs; eval `max(0, L0 + L1·N)`.  
**Gateway:** `src/client/render_rc_ws.c` (`Flow id: rc-ws`); tick orchestration in `src/client/render.c`.

## Pipeline (current)

```
look-at clip origin → gbuffer (UVW)
→ rebuild_prims → tex_prim
→ per cascade: SDF sphere-trace fill
→ T-merge far → near
→ L1 SH encode
→ soft-nearest SH×N → screen irr
→ compose
```

**Prim pack** (`PRIM_COLS=6` × `PRIM_MAX`): center+type, half, quat, lit+rough, albedo+metal, emit.  
Fill: `p_local = Rᵀ(p−c)` → `sdBox`/`sdSphere`. Pose matches `instanceTransform`.

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
| **6.2** | Analytic SDF fill from described meshes | **done** |
| **6.3** | SVO / spatial index (optional) | **later** |
| **B** | Sparse / clipmap probes | **later** |
| — | Bloom halo from gbuf glow | **later** (polish; not GI) |
| — | Holographic HRC | **optional later** |

### Phase 6 — intent

Same RC model (fill → merge → SH → resolve). Change **geometry backend** (Track A) and later **where probes exist** (Track B).

| Track | Work | State |
|-------|------|--------|
| **A** | Look-at clip + CPU stamp | **6.1 done** |
| **A** | Analytic SDF prims (pose + PBR); dense vox demoted | **6.2 done** |
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
| **6.2.0** | Prim contract + upload | **done** |
| **6.2.1** | SDF sphere-trace fill (pose-aware) | **done** |
| **6.2.2** | Fill hits from packed emit + albedo (metal cuts bounce) | **done** |
| **6.2.3** | Prim AABB early-out in march; step budget | **later** |
| **6.2.4** | Demote dense `tex_vox` / `rt_vox` off product path | **done** |

### SVO (6.3 — not 6.2)

Use later for empty-space skip, large worlds, or **sparse probe keys** (occupied leaves). Optional refine: SVO large step → analytic SDF near leaves.

### Landed / next

- **Done:** prims, SDF fill, rotation, demote vox, emit/albedo hits, softer resolve, compose GI balance.
- **Quality (landed with 6.2.2):** wider cascade intervals; resolve softstep face blend; direct ×0.42 so bleed reads.
- **Next:** (1) **6.2.3** march AABB early-out if cost bites; (2) **Track B** sparse/clipmap probes; (3) **6.3** SVO if needed.

---

## Dropped / deferred

| Item | Status | Notes |
|------|--------|-------|
| SS bilinear-fix / amortize / miss←WS | **shelved** | `ss_weight=0` |
| Per-pixel dir loop resolve | **dropped** | Replaced by SH |
| Full 8-tap trilinear of **dir-avg RGB** volume | **dropped** | Hotspots; SH trilinear is OK |
| Wiki bilinear-fix WS merge | **deferred** | 1:1 texel T-merge |
| Screen butter | **deferred** | Not in tick |
| Glow as SS emitters / XZ flatland GI | **rejected** | |
| L2+ SH | **deferred** | |
| Bloom | **deferred** | |
| Frustum-AABB–centered brick | **rejected** | Far-corner swing; use look-at (6.1) |
| Dense vox product geometry | **demoted 6.2.4** | Removed from tick/alloc |
| Floor special-case skip | **rejected** | Same entity → same prim path |
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
- Soft-nearest sequential axis mixes on Y+Z atlas (per-cell shear gradients)

## References

- https://radiance.wiki/ · direction-first · bilinear-fix · [Holographic RC](https://radiance.wiki/variants/holographic-rc)  
- https://jason.today/rc · https://mini.gmshaders.com/p/radiance-cascades  
- Sparse 3D RC (Sannikov): hashmap / screen-visible probes; SDF or HW RT backend  
- Split Radiance Cascades: [arXiv:2607.20384](https://arxiv.org/abs/2607.20384)  
- https://m4xc.dev/articles/fundamental-rc/ · arXiv:2408.14425 · arXiv:2505.02041  

<!-- agent: composer-2.5 | 2026-08-10 | doc resolve trilinear note | 5101c2 -->
