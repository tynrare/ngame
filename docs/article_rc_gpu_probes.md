<!-- agent: composer-2.5 | 2026-08-12 | docs GPU copies surface split | f56309 -->
# Sparse probes & GPU feedback (ngame notes)

Pair with [`docs/radiance-cascades-3d.md`](radiance-cascades-3d.md), `src/client/render_rc_ws.c`.

Performance north star: **probe work ∝ resident slots**, not screen pixels. Seed from **GPU-culled prim AABBs** via `tex_prim_vis`.

**Hot path (shipping):** GPU cull → expand `tex_inst_vis` → VS draw cull → GPU probes from `tex_prim_vis`: Gen0 shell cover → gen waves with **split-merge** (retain unsplit parents; child keep = AABB+shell like CPU `cell_overlaps_vis`) → meta + **open-address** hash (fingerprint skip) → gbuf. **No hot-path texture readback / no CPU vis or SDF apply.**

---

## Takeaways for Track B

| Source | What we copy | What we reject |
|--------|--------------|----------------|
| Sparse 3D RC / Split RC | World spatial hash; near surfaces only; bounded slot count | Per-frame CPU depth readback seed |
| Lumen | Stable cache for static; partial update for movers; GPU feedback later | Full CPU hierarchy walks on hot path |
| Nanite | Retained structure; cull then refine | Resolution-tied placement cost |
| DDGI / cascaded volumes | Fixed probe budget | Dense N³ / look-at clip as long-term default |
| Wright / GI-1.0 | World/hash for persistence | Treating gather cost as probe-pool cost |
| CPU `surface_tick` | Per-parent split; budget stop; open-address hash | Full-pool child-only replace |

---

## Anti-patterns (from literature vs our hot path)

| Bad | Why | Prefer |
|-----|-----|--------|
| Any hot-path `LoadImageFromTexture` | Sync stall | Keep vis/residency on GPU |
| CPU draw filter / CPU vis `UpdateTexture` | Duplicates GPU cull | VS sample `tex_inst_vis` |
| CPU SDF cover/insert for probes | Decode/rework | GPU cover → keep → split-merge → hash RTs |
| Gen wipe → first-N children only | Thins density / grey gaps | Split-merge retain parents |
| Per-frame probe wipe | Wastes stable slots | Fingerprint skip |
| Placement cost ∝ resolution | Violates discrete-probe budget | Deduped keys; work ∝ `slot_cap` |
| Full-clip always-cover | Fights camera motion | Culled-surface cover → gen waves |
| Look-at clip cube as GI volume | Artificial root | Absolute world keys |

---

## Mapping to ngame phases

| Phase | State |
|-------|--------|
| GPU cull + VS draw cull (no vis readback) | **shipping** |
| GPU probes copy CPU split (`surface_tick` semantics) | **shipping** |
| Gbuf after probes; fill / resolve | **later** |
| B.3–B.5 hash spine | retained as storage |
| B.6–B.6.6 clip always-cover | **deprecated** |

**Ship path:** GPU frustum cull → expand inst vis → VS cull draw → GPU probes (split-merge) → gbuf.

<!-- agent: composer-2.5 | 2026-08-12 | docs GPU copies surface split | f56309 -->
