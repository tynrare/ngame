<!-- agent: grok-4.6 | 2026-08-21 | north star chunked static RC | 32d3bc -->
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

**GI:** per visible chunk, run **hierarchical interpolated RC**: fill
intervals, **T-merge** with **trilinear** parent probe interpolation, then
SH/resolve to gbuf.

Gateway (to retarget): `src/client/render_rc_ws.c` (`Flow id: rc-ws`).

Digest: [`docs/article_rc_gpu_probes.md`](article_rc_gpu_probes.md).

## What ships / target

<!-- agent: grok-4.6 | 2026-08-21 | docs cell 1m chunk 8m | 061f06 -->
| Piece | Target |
|-------|--------|
| World grid | Static; `CELL` = 1 m. Chunk = 8×8×8 cells = **8 m cube** |
| Cell ID | Perfect hash from chunk-local UVW → atlas texel `h` |
| Chunk cull | Chunk AABB vs camera frustum (+ prim overlap) |
| Cascades | Classic RC: spacing ×2, interval ×2, dirs ×4 per level |
| Merge | Hierarchical T-merge; interpolate parent (not nearest-only) |
| Geometry | SDF prims ≤2048; BVH on scene dirty |
| Draw cull | CPU BVH frustum → inst bitset + `tex_prim_vis` |
| Compose | Direct + RC irradiance (GI on) |
| Debug | chunks / probes / cascade atlas |

## Pipeline

```
cold: scene dirty → prims + BVH
→ 1) CPU BVH: frustum → inst vis (draw)
→ 2) enumerate world chunks overlapping frustum (+ vis prim AABBs)
→ 3) for each visible chunk:
      pack 8³ cells by UVW hash h
      fill cascade 0..C (SDF march per probe×dir)
      merge C-1 ← C … ← 0 with trilinear parent sample + T
      encode SH / write chunk cache
→ 4) gbuf
→ 5) resolve: trilinear sample nearest cascades at shading point
→ compose
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

Per visible chunk (and optionally coarser world cascades that span many chunks):

1. **Fill** cascade `c`: probes on the grid with spacing `CELL * 2^c`;
   each probe traces `dirs(c)` cones on interval `[t0(c), t1(c)]`.
2. **Merge** `c` with parent `c+1`: leftover transmittance `T = 1 - a_near`
   multiplies **interpolated** parent radiance (8-tap 3D).
3. **Resolve** at a surface: interpolate probes of the finest cascade that
   covers the point; fall back to coarser if needed.

Classic RC (Sannikov): hierarchy + interpolation. Screen-space cascade
atlases are not the GI volume.

## Out of scope

Sparse surface octree, persistent slot pool, cover/split/steal/relax,
clip always-cover, look-at GI cube, KD free cubes, GI-offline compose as
the product path. Code still has leftovers; this doc is the replacement
target, not a description of that code.

<!-- agent: grok-4.6 | 2026-08-21 | north star chunked static RC | 32d3bc -->
<!-- agent: grok-4.6 | 2026-08-21 | docs cell 1m chunk 8m | 061f06 -->
