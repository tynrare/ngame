<!-- agent: grok-4.6 | 2026-08-21 | live path is chunk GI only | 70ea5a -->
<!-- agent: grok-4.6 | 2026-08-21 | north star chunked static RC | 32d3bc -->
<!-- agent: grok-4.6 | 2026-08-21 | north star lights shadows | ed07d5 -->
# Radiance Cascades (3D) — North Star

Goal: **dynamic, deterministic GI** (WebGL2/GLES3). Quality = **cost only**.

Product path = **classic hierarchical, interpolated Radiance Cascades** on a
**static world grid**, tiled into **chunks**.

**Grid:** world is partitioned into axis-aligned chunks of **N³ cells**
(default **N = 8**). A cell’s address is its local UVW:

`ix,iy,iz = floor(uvw * N)`, `uvw = (p - chunk_origin) / chunk_extent`

That integer triple is a **perfect hash** into the chunk atlas:

`h = ix + N * (iy + N * iz)`  ∈ `[0, N³)`

No open-address table, no slot steal, no surface octree. Empty cells stay
empty texels; occupancy is implicit.

**Cull:** frustum-test **chunk AABBs** (and skip chunks with no overlapping
visible prims). Probe fill/merge only for surviving chunks. Mesh draw cull
stays CPU BVH → inst bitset.

**GI:** per visible chunk, **classic RC**: fill intervals, **T-merge**
(trilinear parents + **4:1 dir match**), resolve **cosine over C0 dirs**.
**Lights in the field** = prim emit + **sky/sun on the last cascade miss**
(merge occludes them). Compose `N·L` is fill-light only; it is not RC.

Gateway (to retarget): `src/client/render_rc_ws.c` (`Flow id: rc-ws`).

Digest: [`docs/article_rc_gpu_probes.md`](article_rc_gpu_probes.md).

## Sources (Sannikov)

RC stores **radiance intervals** on a probe hierarchy so the field stays
linearly interpolable (Nyquist / **penumbra**): near field needs **dense
probes, few dirs**; far field needs **sparse probes, many dirs**. Merge
builds long rays from short ones via interpolated parents.

Emissive surfaces and environment are the RC light sources. Sky **and**
directional sun belong on the **coarsest** cascade so `T`-merge can shadow
them. Soft contact shadows **are** that field (hit α + `T = 1 - a_near`).
Compose Lambert on gbuf `N` is not a cascade and must not be the picture.

Refs: [radiance.wiki](https://radiance.wiki/papers/sannikov-original)
(Sannikov preprint), Osborne & Sannikov bilinear/parallax fix,
[GM Shaders RC](https://mini.gmshaders.com/p/radiance-cascades).

<!-- agent: grok-4.6 | 2026-08-21 | docs proper RC dirs merge | cce8a5 -->
## Lights

- **RC:** hit = `emit×boost + albedo × previous field` (not albedo-as-lamp).
  Last-cascade **miss** = directional sky + sun **lobe** (`ω`). Inner misses
  stay dark until merge. Bounce is the field, not compose.
- **Direct:** compose `N·L` **off**. Sun-in-GI is the C2 miss lobe.

## Shadows

- **GI / penumbra:** fill α + **full** `C_n ← … ← C0` T-merge. Parents are
  **world** 8-tap; dirs are **4 child ↔ 1 parent** on the sphere (not
  Fibonacci index mix). Bilinear/parallax fix is required for merge
  continuity (Osborne & Sannikov).
- **Resolve:** world-cell 8-tap (neighbor chunks); **cosine over merged C0
  dirs** (`Σ L(ω) max(N·ω, 0)`). Averaging RGB or L1 from 6 dirs is not RC.
- **Hard contact:** only extra compose direct. RC does not use a shadow map.

## Target vs live

<!-- agent: grok-4.6 | 2026-08-21 | docs C2 sun lobe live | 25ad91 -->
<!-- agent: grok-4.6 | 2026-08-21 | docs live cosine C0 resolve | 344dba -->
<!-- agent: grok-4.6 | 2026-08-21 | docs cell 1m chunk 8m | 061f06 -->
<!-- agent: grok-4.6 | 2026-08-21 | docs live dirs 4to1 merge | 044b4f -->
| Piece | Target (proper RC) | Live (gap) |
|-------|-------------------|------------|
| World grid | Static; `CELL` = 1 m; chunk **8 m** | same |
| Cell ID | UVW perfect hash `h` | same |
| Chunk cull | AABB + vis prim overlap | same |
| Cascades | spacing ×2, interval ×2, **dirs ×4** (`6,24,96`) | `6,24,96` |
| Merge | every level; **4:1 dirs** + world 8-tap + bilinear/parallax | 8-tap + **4 nearest parent dirs**; no fix |
| Sky / sun | last cascade miss only; sun is a **dir lobe** | C2 miss: sky + **sun lobe** |
| Last `t1` | long enough to reach env (≫ 16 m) | C2 ends **64 m** |
| Resolve | cosine **C0 dirs** (or L1 from merged dirs) | cosine over merged **C0 6 dirs** |
| Bounce | albedo × previous **merged** field | albedo × SH (fill) |
| Geometry | SDF prims ≤2048 | fill loops **32** prims |
| Compose | `kd * E_rc` (+ optional dim fill-light) | `kd * E_rc` + ambient + glow |
| Cost | dirty pages; ∝ vis chunks × N³ × dirs | 2 pages/frame |

## Pipeline

```
cold: scene dirty → prims + BVH
→ 1) CPU BVH: frustum → inst vis (draw)
→ 2) enumerate world chunks overlapping frustum (+ vis prim AABBs)
→ 3) for each visible chunk:
      pack 8³ cells by UVW hash h
      fill cascade 0..C (SDF march per probe×dir)
      sky+sun lobe → last cascade miss only
      merge C-1 ← C … ← 0: T × world 8-tap × 4:1 dirs
      write merged C0 (optional L1 from merged dirs)
→ 4) gbuf
→ 5) resolve: world-cell 8-tap, cosine over C0 dirs
→ compose kd * E_rc [+ optional dim N·L]
```

## Static grid → chunks

Baseline is a **regular lattice**, not a sparse hash of surfaces.

- **Chunk** = cube of `N×N×N` cells (`N=8`, `CELL=1` m → **8 m** extent, 512 probes/chunk at cascade 0).
- **World key** = `(cx,cy,cz, h)` or packed `chunk_id * N³ + h`.
- **UVW** is local to the chunk; hash is bijective on in-chunk cells.
- Camera motion does **not** rekey cells. Chunks are world-fixed.
- No look-at clip cube. No dual near/far coverage volumes.

`tex_grid` as a **prim-candidate stamp over one clip cube** is not this
lattice. Probe identity is the chunk UVW hash; prim accel is optional later.

## Hierarchical interpolated RC

Per visible chunk, plus **coarser world cascades that span many chunks**
(chunk C2 is not infinity):

1. **Fill** cascade `c`: probes on the grid with spacing `CELL * 2^c`;
   `dirs(c) = 6 × 4^c` cones on `[t0(c), t1(c)]`. Last cascade miss stores
   sky(`ω`) + sun(`ω`).
2. **Merge** `c` with parent `c+1`: `T = 1 - a_near` multiplies **8-tap**
   parent radiance. Each near dir maps to **four** parent dirs on the
   sphere (GM Shaders 4:1; not a 1D index lerp). Reproject for
   bilinear/parallax. Parents are **world-addressed**.
3. **Resolve** at a surface: world-cell trilinear; **`Σ L(ω) max(N·ω, 0)`**
   over merged C0 dirs.

Classic RC (Sannikov): hierarchy + interpolation. Screen-space cascade
atlases are not the GI volume. Compose `N·L` is not a cascade.

## Out of scope

Sparse surface octree, persistent slot pool, cover/split/steal/relax,
clip always-cover, look-at GI cube, KD free cubes, GI-offline compose as
the product path. Shadow maps as GI. Screen-space RC as the volume.
Compose Lambert / dual keys **as the lighting**. Per-pixel path tracing.

Live code is chunk GI only (`src/client/render_rc_ws.c`, `Flow id: rc-ws`):
CPU BVH cull, world-fixed pages, hierarchical fill/merge/resolve. No
`NG_RC_WS_GI_OFFLINE` dual path.

<!-- agent: grok-4.6 | 2026-08-21 | docs C2 sun lobe live | 25ad91 -->
<!-- agent: grok-4.6 | 2026-08-21 | docs live cosine C0 resolve | 344dba -->
<!-- agent: grok-4.6 | 2026-08-21 | docs live dirs 4to1 merge | 044b4f -->
<!-- agent: grok-4.6 | 2026-08-21 | docs proper RC dirs merge | cce8a5 -->
<!-- agent: grok-4.6 | 2026-08-21 | docs compose sun ships unshadowed | 054117 -->
<!-- agent: grok-4.6 | 2026-08-21 | docs albedo times previous SH | 1fa760 -->
<!-- agent: grok-4.6 | 2026-08-21 | live path is chunk GI only | 70ea5a -->
<!-- agent: grok-4.6 | 2026-08-21 | north star lights shadows | ed07d5 -->
<!-- agent: grok-4.6 | 2026-08-21 | docs cell 1m chunk 8m | 061f06 -->
