<!-- agent: composer-2.5 | 2026-08-10 | doc uniform prim grid | 2b41bd -->
<!-- agent: composer-2.5 | 2026-08-10 | doc Track B clipmap plan | ba2f47 -->
<!-- agent: composer-2.5 | 2026-08-10 | B1 doc clipmap shipped | 41a4da -->
<!-- agent: composer-2.5 | 2026-08-10 | B2 doc amortize shipped | ccf229 -->
<!-- agent: composer-2.5 | 2026-08-10 | roadmap B3-B5 sparse hierarchy | a30c7a -->
<!-- agent: composer-2.5 | 2026-08-10 | B3 doc sparse hashmap path | 882e2d -->
# Radiance Cascades (3D) — North Star

Goal: **dynamic, deterministic GI** (WebGL2/GLES3). Quality = **cost only**.
Product path = **world-space Radiance Cascades**.  
Compose: `ambient×1 + directional×1 + gi_strength×1·kd·irr + glow`.

**End state (Track B):** sparse hierarchical probe cache — near cells tiny, far huge; LOD from **depth + SDF curvature**; rebuild only dirty / newly visible cells; collapse = **average children** (no full recalc); free OOV cells and reassign; optional **AO** from same probes.

## What ships (aligned with code)

| Piece | Implementation |
|-------|----------------|
| Gbuffer | albedo+**rough(A)** / normal+**metal(A)** / glow / depth (`RGB=UVW`, `A=inside` **far**) |
| Clip | Far look-at cube (near nested for scroll); CELL / 2×CELL snap; hysteresis |
| Geometry | Analytic SDF prims (`tex_prim`); col1.a = bound R |
| Grid | **8³** uniform over **far**; **4** slots/cell (`tex_grid` RGBA8); dirty-gated |
| Sparse | Screen-seeded CELL keys → slot pool; `tex_meta` + open-address `tex_hash` |
| Fill | Atlas **W=dirs × H=slots**; SDF march from meta centers |
| Merge / SH / Resolve | T-merge → L1 SH rows; resolve **hash probe** (+ face neighbor) |
| Compose | `AMBIENT=1`, `DIRECTIONAL=1`; `gi_strength` default 1 |
| Quality | Scales **slots / dirs / cascades / steps** |

**Upload:** prim + grid dirty-gated; meta/hash each frame from 64² depth seed.  
**Gateway:** `src/client/render_rc_ws.c` (`Flow id: rc-ws`).  
**Debug:** `set debug.render.pass` → `final|albedo|normal|glow|depth|irradiance|uvw|probes|grid|atlas`.

## Pipeline

```
far look-at cube → gbuf (UVW vs far)
→ dirty? rebuild_prims + rebuild_grid
→ downsample depth 64² → CPU unique CELL keys + hashmap → tex_meta/hash
→ sparse fill/merge/SH → hash resolve → compose
```

## Phases

| Phase | Goal | State |
|------:|------|--------|
| 0–5 | Scene → WS RC + SH | **done** |
| **6.1–6.2** | Look-at clip + SDF prims | **done** |
| **Look / P2** | Bounce, PBR, self-bias | **done** |
| **6.2.3** | Bound early-out | **done** |
| **Grid** | Uniform candidate lists | **done** |
| **BVH** | If slot overflow / ≫64 | **deferred** |
| **B.1** | Clipmap dual volumes | **done** (stabilize skipped; B.3 supersedes fill) |
| **B.2** | Amortize far fill | **done** (dense era) |
| **B.3** | Sparse screen-seeded hashmap | **done** |
| **B.4** | Hierarchy / SVO collapse-avg | later |
| **B.5** | Depth+curvature LOD + dirty + AO | later |

---

## Track B — probe placement / storage

| Spine | Idea | Status |
|-------|------|--------|
| **B.1** clipmap | Near fine + far 2× dense volumes; resolve blend | **done** |
| **B.2** amortize | Near every frame; far period by quality | **done** |
| **B.3** sparse | GPU downsample depth → CPU keys/hash → sparse fill/resolve | **done** |
| **B.4** hierarchy | SVO empty-skip; leaf = key; **collapse = avg children**; split = alloc+fill | later |
| **B.5** adaptive | LOD from depth+curvature; dirty cells only; near tiny / far huge; AO | later |

### B.1–B.2 shipped (dense baseline, superseded for fill)

- Near/far look-at cubes; gbuf vs far; prim/grid on far.
- Dense dual fill retired by B.3.

### B.3 — sparse keys (shipped)

One path (no GPU/CPU mode switch):

1. Blit `rt_depth` → 64² seed; `ReadPixels` that only.
2. CPU: unique `floor(p/CELL)` keys + face pad; slot pool by quality (128…512).
3. Upload `tex_meta` (center+occ) + `tex_hash` (slot+1, cell xyz).
4. Fill/merge/SH on **dirs × slots** atlas; resolve linear-probe hash (face fallback).
5. OOV dropped by full reseed each frame.

### B.4 — hierarchy (toward B.5)

- SVO / octree over world: empty nodes skipped; occupied **leaves** are B.3 keys.
- **Collapse:** parent SH = average of children (no refill).
- **Split:** allocate child keys + fill when detail needed.
- Enables continuous **near tiny / far huge** without two hard clip volumes.

### B.5 — adaptive cache (end goal)

- LOD from **depth + SDF curvature** (high curve / near cam → finer leaves).
- Rebuild **only** dirty cells (cam move, movers, new visibility); free OOV → reassign.
- Optional **AO** from same sparse probes in this pass.
- Quality only changes budget / max depth / dirs — not algorithm.

Refs: Sparse 3D RC (Sannikov), Split RC arXiv:2607.20384, DDGI cascaded volumes.

---

## Look / self-bias (current)

| Knob | Default |
|------|---------|
| `AMBIENT` / `DIRECTIONAL` | 1 |
| `AMBIENT_ALBEDO` | 0.22 |
| Fill bounce / E_LIT / emit | ~1.65 / ~1.35 / ~6 |
| `SELF_T_MIN` / resolve `SELF_BIAS` | ~0.1 / ~0.09∨0.25·cell |
| Grid | 8³ × 4 over far; empty=255 |
| Slots (q0–q4) | 128 / 192 / 256 / 384 / 512 |

---

## Further plans

| Priority | Item |
|---------:|------|
| 1 | **B.4** hierarchy collapse-avg / split-fill |
| 2 | **B.5** depth+curvature LOD + dirty + AO |
| 3 | BVH if 4 slots overflow often |
| 4 | Bloom / L2 SH |

## Anti-patterns

- Rebuilding prim/grid every orbit frame (use dirty gate)  
- Cam-follow clip; clamped UVW  
- BVH before grid proves insufficient  
- Full dense refill when only a few cells dirty (B.5)  
- Collapse that re-marches instead of averaging children (B.4)  
- Full-screen depth readback (seed is 64² only)  

## References

- https://radiance.wiki/ · jason.today/rc · Split RC · DDGI self-bias  

<!-- agent: composer-2.5 | 2026-08-10 | doc uniform prim grid | 2b41bd -->
<!-- agent: composer-2.5 | 2026-08-10 | doc Track B clipmap plan | ba2f47 -->
<!-- agent: composer-2.5 | 2026-08-10 | B1 doc clipmap shipped | 41a4da -->
<!-- agent: composer-2.5 | 2026-08-10 | B2 doc amortize shipped | ccf229 -->
<!-- agent: composer-2.5 | 2026-08-10 | roadmap B3-B5 sparse hierarchy | a30c7a -->
<!-- agent: composer-2.5 | 2026-08-10 | B3 doc sparse hashmap path | 882e2d -->
