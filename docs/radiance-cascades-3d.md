<!-- agent: composer-2.5 | 2026-08-11 | B66 want-have balanced depth | 6eae0e -->
# Radiance Cascades (3D) — North Star

Goal: **dynamic, deterministic GI** (WebGL2/GLES3). Quality = **cost only**.
Product path = **world-space Radiance Cascades**.

**Performance contract:** placement ∝ `slot_cap`. Idle fp → zero work. Dirty → one score → K-list apply. Soft 25% reserve; never freeze. Bandwidth O(K).

**Coverage invariant:** every point in the far clip has a covering leaf. Camera / frustum only changes **leaf depth**, never punches holes.

Digests: [`docs/article_rc_gpu_probes.md`](article_rc_gpu_probes.md).

## What ships

| Piece | Implementation |
|-------|----------------|
| Clip | Far look-at cube |
| Geometry | SDF prims; BVH scene-dirty only |
| Sparse | **B.6.6** always-cover + collapse/split; `tex_meta`/`tex_hash`/`tex_prio` |
| Fill | Dirty-only; skip if dirty==0 |
| Resolve | Covering leaf + soft parent |

**Gateway:** `src/client/render_rc_ws.c` (`Flow id: rc-ws`).

## Pipeline

```
gbuf → scene dirty? prims+BVH+grid
→ sparse_tick (fp dirty):
     cover full clip AABB
     → one score (coarsen=far/OOV; promote=in-view·1/d)
     → apply K: collapse for reserve/fuel then split
     → meta/hash if changed
→ dirty>0? fill : skip → resolve
```

## Phases

| Phase | State |
|------:|--------|
| B.1–B.5 | **done** |
| B.6–B.6.5 | **superseded** (evict = holes) |
| **B.6.6** always-cover collapse/split | **shipping** |

### B.6.6 — always covered; balanced depth

<!-- agent: composer-2.5 | 2026-08-11 | B66 want-have balanced depth | 6eae0e -->

| Demand | Trigger | Op |
|--------|---------|-----|
| Coarsen | over-fine vs want(d), fine depth, OOV/far, or free &lt; reserve / split fuel | collapse siblings → parent |
| Promote | in frustum · have&gt;want · lod&gt;0 | split |

**Balance:** promote only when coarser than distance want; dispose priority ∝ detail (`fine²` + over-fine). Interleave collapse and split within K — never dump the whole pool into one lineage.

Frustum / behind bias scores only. In-clip leaf is never deleted without a coarser cover.

## Optimization / quality plan

| Priority | Item |
|---------:|------|
| **0** | **B.6.6** always-cover collapse (**shipping**) |
| 1 | GPU score + K×1 `tex_ops` readback |
| 2 | Exact clip-matrix frustum scoring |
| 3 | SDF/curv promote; SH-average collapse |
| 4 | Full GPU apply; brick SDF |

## Anti-patterns

- **Evict / free in-clip leaf** (leave miss)  
- Steal gated on promote; fill when dirty==0; screen-pixel seeds  
- Cover only in-frustum (holes behind / at sides)  

<!-- agent: composer-2.5 | 2026-08-11 | B66 always-cover collapse docs | e2101c -->
<!-- agent: composer-2.5 | 2026-08-11 | B66 want-have balanced depth | 6eae0e -->
