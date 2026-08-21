<!-- agent: grok-4.6 | 2026-08-21 | digest chunked static RC | 54f7ce -->
<!-- agent: grok-4.6 | 2026-08-21 | digest emit sky penumbra | 92ea4e -->
# Chunked static RC (ngame notes)

Pair with [`docs/radiance-cascades-3d.md`](radiance-cascades-3d.md).

**Path:** static world lattice → **8³ chunks** → cell **perfect hash from UVW**
→ frustum **chunk AABB cull** → **classic hierarchical interpolated RC**.

**Hot path (target):** CPU BVH draw cull → visible chunks → fill/merge/SH per
chunk → gbuf → trilinear resolve (`N · E`) → compose `kd * E_rc` (+ optional
direct). **Lights** = emit + sky on last cascade. **Shadows** = RC penumbra
(interval α + T-merge), not a map.

**Hard rules:** no sparse surface residency; no open-address probe hash; no
look-at clip as the GI volume; no hot-path CPU freelist. Work ∝ visible
chunks × N³ × dirs.

| Copy | Reject |
|------|--------|
| Dense world lattice; UVW → `h = ix + N*(iy + N*iz)` | Open-address slots, steal, surface octree |
| Chunk AABB cull | Per-pixel / depth-seed probe placement |
| Hierarchical T-merge + trilinear parent | Nearest-only merge; screen atlas as volume |
| Emit + sky (last cascade); optional analytic direct | Fake dual compose keys as lighting |
| Bounded cost ∝ vis chunks | Per-frame full-world wipe; shadow map as GI |

<!-- agent: grok-4.6 | 2026-08-21 | digest chunked static RC | 54f7ce -->
<!-- agent: grok-4.6 | 2026-08-21 | digest emit sky penumbra | 92ea4e -->
