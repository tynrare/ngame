<!-- agent: composer-2.5 | 2026-08-12 | article incremental GPU residency | 3d4f8c -->
<!-- agent: composer-2.5 | 2026-08-12 | article lazy stochastic rebalance | 6ca44f -->
<!-- agent: grok-4.6 | 2026-08-12 | even split steal occupancy log | ecfe9c -->
<!-- agent: grok-4.6 | 2026-08-12 | docs lazy persistent cover | 3e3716 -->
<!-- agent: grok-4.6 | 2026-08-12 | docs CPU oracle smoke | dd1744 -->
<!-- agent: grok-4.6 | 2026-08-12 | docs persist cover relax | a4f0c2 -->
<!-- agent: composer-2.5 | 2026-08-13 | article BVH scale cull | d7f1b3 -->
<!-- agent: composer-2.5 | 2026-08-13 | article unified GPU cull readback | b8747a -->
<!-- agent: composer-2.5 | 2026-08-13 | article CPU BVH cull upload | 5a4e40 -->
<!-- agent: composer-2.5 | 2026-08-13 | article GPU split gate no readback | c8f3a1 -->
# Sparse probes & GPU feedback (ngame notes)

Pair with [`docs/radiance-cascades-3d.md`](radiance-cascades-3d.md), `src/client/render_rc_ws.c`.

Performance north star: **probe work ∝ resident slots**, not screen pixels. Seed from **CPU-culled** geometry via uploaded `tex_prim_vis`.

**Hot path (shipping):** CPU BVH cull → upload `tex_prim_vis` → **persistent probes** (union → release → cover → unmet → relax → cover → split → steal excess → meta/hash) → CPU batch-filtered gbuf. Persist keys; cover-first. CPU oracle: `ng_rc_ws_probe_smoke`. MCP: `probe_snapshot`.

**Hard rules:** no CPU on probe residency hot path; no hot-path `LoadImageFromTexture` of probe RTs (split gate + occupancy log on GPU; MCP `probe_snapshot` on demand). Cull = CPU traverse; one `UpdateTexture` for `tex_prim_vis` only. Opt-in occupancy TraceLog: `set debug.logging.probes 1`.

**Reuse:** cold `tex_prim` + `tex_bvh` + `tex_inst_prim`; hot `tex_prim_vis` (CPU upload). BVH for CPU frustum cull (+ future SDF).

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
| CPU vis readback for draw | Sync stall / fragile on GLES | CPU traverse inst bitset |
| GPU BVH cull + vis readback | Duplicates CPU; black-screen risk | `ng_rc_ws_cull_traverse` + `ng_rc_ws_upload_prim_vis` |
| VS `ng_inst_cull` in gbuf | Second cull simulation | Batch filter only; debug uses `tex_prim_vis` |
| O(inst×prim) inst vis expand | O(n²) at scale | O(1) `tex_inst_prim` map |
| Per-frame probe wipe | Wastes stable slots | Persistent pool; cold clear only |
| Hot CPU freelist / hash pack | Bandwidth stall | GPU slots + GPU hash pass |
| Full-clip always-cover | Fights camera motion | Vis-union release + cover |
| Look-at clip cube as GI volume | Artificial root | Absolute world keys |

---

## Mapping to ngame phases

| Phase | State |
|-------|--------|
| CPU BVH cull + batch draw | **shipping** |
| GPU incremental probe residency | **shipping** |
| Compact `tex_vis_prim_list` for probe FS loops | **later** |
| Gbuf after probes; fill / resolve | **later** |
| B.3–B.5 hash spine | retained as storage |
| B.6–B.6.6 clip always-cover | **deprecated** |

**Ship path:** CPU BVH cull → upload prim_vis → incremental probes → gbuf.

<!-- agent: composer-2.5 | 2026-08-13 | article GPU split gate no readback | c8f3a1 -->
<!-- agent: composer-2.5 | 2026-08-13 | article CPU BVH cull upload | 5a4e40 -->
<!-- agent: composer-2.5 | 2026-08-13 | article unified GPU cull readback | b8747a -->
<!-- agent: composer-2.5 | 2026-08-13 | article BVH scale cull | d7f1b3 -->
<!-- agent: composer-2.5 | 2026-08-12 | article lazy stochastic rebalance | 6ca44f -->
<!-- agent: grok-4.6 | 2026-08-12 | even split steal occupancy log | ecfe9c -->
<!-- agent: grok-4.6 | 2026-08-12 | docs lazy persistent cover | 3e3716 -->
<!-- agent: grok-4.6 | 2026-08-12 | docs CPU oracle smoke | dd1744 -->
<!-- agent: grok-4.6 | 2026-08-12 | docs persist cover relax | a4f0c2 -->
