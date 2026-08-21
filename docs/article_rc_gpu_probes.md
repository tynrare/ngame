<!-- agent: grok-4.6 | 2026-08-21 | digest proper RC merge cosine | f2882a -->
<!-- agent: grok-4.6 | 2026-08-21 | digest chunked static RC | 54f7ce -->
# Chunked static RC (ngame notes)

Pair with [`docs/radiance-cascades-3d.md`](radiance-cascades-3d.md).

**Path:** static world lattice → **8³ chunks** → cell **perfect hash from UVW**
→ frustum **chunk AABB cull** → **classic hierarchical interpolated RC**.

**Hot path (target):** CPU BVH draw cull → visible chunks → fill → **T-merge
(world 8-tap, dirs ×4 / 4:1)** → cosine resolve C0 dirs → compose
`kd * E_rc`. **Lights in the field** = emit + sky/**sun lobe** on last
cascade miss. **Shadows** = interval α + merge (penumbra). Compose `N·L`
is optional fill-light, not RC.

**Hard rules:** no sparse surface residency; no open-address probe hash; no
look-at clip as the GI volume; no hot-path CPU freelist. Work ∝ visible
chunks × N³ × dirs.

| Copy | Reject |
|------|--------|
| Dense world lattice; UVW → `h = ix + N*(iy + N*iz)` | Open-address slots, steal, surface octree |
| Chunk AABB cull | Per-pixel / depth-seed probe placement |
| T-merge + 4:1 dirs + trilinear parent | Index-lerp dirs; nearest-only; screen atlas as volume |
| Emit + sky/sun on last cascade | Compose Lambert / dual keys as lighting |
| Cosine over merged C0 dirs | L1 from 6 fill dirs as the picture |
| Bounded cost ∝ vis chunks | Per-frame full-world wipe; shadow map as GI |

<!-- agent: grok-4.6 | 2026-08-21 | digest proper RC merge cosine | f2882a -->
<!-- agent: grok-4.6 | 2026-08-21 | digest chunked static RC | 54f7ce -->
