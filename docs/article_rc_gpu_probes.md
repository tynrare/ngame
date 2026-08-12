<!-- agent: composer-2.5 | 2026-08-12 | docs single-root GPU probes | 28e4b5 -->
# Sparse probes & GPU feedback (ngame notes)

Pair with [`docs/radiance-cascades-3d.md`](radiance-cascades-3d.md), `src/client/render_rc_ws.c`.

Performance north star: **probe work ∝ resident slots**, not screen pixels. Seed from **GPU-culled** geometry via `tex_prim_vis`.

**Hot path (shipping):** GPU cull → expand `tex_inst_vis` → VS draw cull → **single-root** probe over union of vis AABBs → even split-merge waves (shell keep on kids; free vs `slot_cap`) → meta + open-address hash (fingerprint skip) → gbuf.

**Reuse:** cold `tex_prim` (SDF pack) + hot `tex_prim_vis`. BVH for cull only — probes do not rebuild prims/BVH/cull.

---

## Takeaways for Track B

| Source | What we copy | What we reject |
|--------|--------------|----------------|
| Sparse 3D RC / Split RC | World spatial hash; near surfaces only; bounded slot count | Per-frame CPU depth readback seed |
| Lumen | Stable cache for static; partial update for movers; GPU feedback later | Full CPU hierarchy walks on hot path |
| Nanite | Retained structure; cull then refine | Resolution-tied placement cost |
| DDGI / cascaded volumes | Fixed probe budget | Dense N³ / look-at clip as long-term default |
| Wright / GI-1.0 | World/hash for persistence | Treating gather cost as probe-pool cost |
| Surface octree | One root → split → shell discard | AABB flood Gen0 / first-N child wipe |

---

## Anti-patterns (from literature vs our hot path)

| Bad | Why | Prefer |
|-----|-----|--------|
| Any hot-path vis `LoadImageFromTexture` | Sync stall | Keep vis on GPU |
| AABB×2048 Gen0 flood | Biased + expensive | Single root from union |
| Early-index budget monopoly on split | Uneven / grey gaps | Even all-leaf split vs `slot_cap` free |
| CPU draw filter / CPU vis `UpdateTexture` | Duplicates GPU cull | VS sample `tex_inst_vis` |
| Per-frame probe wipe | Wastes stable slots | Fingerprint skip |
| Placement cost ∝ resolution | Violates discrete-probe budget | Deduped keys; work ∝ `slot_cap` |
| Full-clip always-cover | Fights camera motion | Culled-surface root → gen waves |
| Look-at clip cube as GI volume | Artificial root | Absolute world keys |

---

## Mapping to ngame phases

| Phase | State |
|-------|--------|
| GPU cull + VS draw cull (no vis readback) | **shipping** |
| GPU single-root probes + even split | **shipping** |
| Gbuf after probes; fill / resolve | **later** |
| B.3–B.5 hash spine | retained as storage |
| B.6–B.6.6 clip always-cover | **deprecated** |

**Ship path:** GPU frustum cull → expand inst vis → VS cull draw → single-root probes → gbuf.

<!-- agent: composer-2.5 | 2026-08-12 | docs single-root GPU probes | 28e4b5 -->
