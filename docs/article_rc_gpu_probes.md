<!-- agent: composer-2.5 | 2026-08-12 | article incremental GPU residency | 3d4f8c -->
<!-- agent: composer-2.5 | 2026-08-12 | article lazy stochastic rebalance | 6ca44f -->
<!-- agent: grok-4.6 | 2026-08-12 | even split steal occupancy log | ecfe9c -->
<!-- agent: grok-4.6 | 2026-08-12 | docs lazy persistent cover | 3e3716 -->
<!-- agent: grok-4.6 | 2026-08-12 | docs CPU oracle smoke | dd1744 -->
<!-- agent: grok-4.6 | 2026-08-12 | docs persist cover relax | a4f0c2 -->
# Sparse probes & GPU feedback (ngame notes)

Pair with [`docs/radiance-cascades-3d.md`](radiance-cascades-3d.md), `src/client/render_rc_ws.c`.

Performance north star: **probe work ∝ resident slots**, not screen pixels. Seed from **GPU-culled** geometry via `tex_prim_vis`.

**Hot path (shipping):** GPU cull → expand `tex_inst_vis` → VS draw cull → **persistent probes** (union → release → cover → unmet → relax → cover → split → steal excess → meta/hash) → gbuf. Persist keys; cover-first. CPU oracle: `ng_rc_ws_probe_smoke`. MCP: `probe_snapshot`.

**Hard rules:** no CPU on probe hot path; no `LoadImageFromTexture` / per-frame `UpdateTexture` of residency RTs. Cold CPU only on scene dirty (`tex_prim` / BVH).

**Reuse:** cold `tex_prim` (SDF pack) + hot `tex_prim_vis`. BVH for cull only.

---

## Takeaways for Track B

| Source | What we copy | What we reject |
|--------|--------------|----------------|
| Sparse 3D RC / Split RC | World spatial hash; near surfaces only; bounded slot count | Per-frame CPU depth readback seed |
| Lumen | Stable cache; partial update; free/steal under budget | Full CPU hierarchy walks on hot path |
| Nanite | Retained structure; cull then refine | Resolution-tied placement cost |
| DDGI / cascaded volumes | Fixed probe budget | Dense N³ / look-at clip as long-term default |
| Wright / GI-1.0 | World/hash for persistence | Treating gather cost as probe-pool cost |
| Surface octree | Split → shell discard; merge/compact | Per-frame wipe / AABB flood Gen0 |

---

## Anti-patterns (from literature vs our hot path)

| Bad | Why | Prefer |
|-----|-----|--------|
| Any hot-path vis `LoadImageFromTexture` | Sync stall | Keep vis on GPU |
| AABB×2048 Gen0 flood wipe | Biased + expensive | Incremental fill free slots |
| Early-index budget monopoly on split | Uneven / grey gaps | Even all-leaf split vs `slot_cap` free |
| CPU draw filter / CPU vis `UpdateTexture` | Duplicates GPU cull | VS sample `tex_inst_vis` |
| Per-frame probe wipe | Wastes stable slots | Persistent pool; cold clear only |
| Hot CPU freelist / hash pack | Bandwidth stall | GPU slots + GPU hash pass |
| Full-clip always-cover | Fights camera motion | Vis-union release + cover |
| Look-at clip cube as GI volume | Artificial root | Absolute world keys |

---

## Mapping to ngame phases

| Phase | State |
|-------|--------|
| GPU cull + VS draw cull (no vis readback) | **shipping** |
| GPU incremental probe residency | **shipping** |
| Gbuf after probes; fill / resolve | **later** |
| B.3–B.5 hash spine | retained as storage |
| B.6–B.6.6 clip always-cover | **deprecated** |

**Ship path:** GPU frustum cull → expand inst vis → VS cull draw → incremental probes → gbuf.

<!-- agent: composer-2.5 | 2026-08-12 | article incremental GPU residency | 3d4f8c -->
<!-- agent: composer-2.5 | 2026-08-12 | article lazy stochastic rebalance | 6ca44f -->
<!-- agent: grok-4.6 | 2026-08-12 | even split steal occupancy log | ecfe9c -->
<!-- agent: grok-4.6 | 2026-08-12 | docs lazy persistent cover | 3e3716 -->
<!-- agent: grok-4.6 | 2026-08-12 | docs CPU oracle smoke | dd1744 -->
<!-- agent: grok-4.6 | 2026-08-12 | docs persist cover relax | a4f0c2 -->
