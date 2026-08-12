<!-- agent: composer-2.5 | 2026-08-11 | B62 GPU prio article map | 75bc1b -->
<!-- agent: composer-2.5 | 2026-08-11 | B63 Nanite cut cover article | 2b8343 -->
<!-- agent: composer-2.5 | 2026-08-11 | article deprecate clip always-cover | b04b5b -->
<!-- agent: composer-2.5 | 2026-08-12 | article GPU hot path vis bridge | 23a111 -->
<!-- agent: composer-2.5 | 2026-08-12 | article zero readback | e6fdf1 -->
# Sparse probes & GPU feedback (ngame notes)

Pair with [`docs/radiance-cascades-3d.md`](radiance-cascades-3d.md), `src/client/render_rc_ws.c`.

Performance north star: **probe work ∝ resident slots**, not screen pixels. Seed from **culled meshes**, not screen-pixel loops.

**Hot path target:** GPU cull → VS draw cull → probes from prev `tex_vis` (fingerprint skip) → gbuf. **No hot-path texture readback.**

---

## Takeaways for Track B

| Source | What we copy | What we reject |
|--------|--------------|----------------|
| Sparse 3D RC / Split RC | World spatial hash; near surfaces only; bounded slot count | Per-frame CPU depth readback seed |
| Lumen | Stable cache for static; partial update for movers; GPU feedback later | Full CPU hierarchy walks on hot path |
| Nanite | Retained structure; cull then refine | Resolution-tied placement cost |
| DDGI / cascaded volumes | Fixed probe budget | Dense N³ / look-at clip as long-term default |
| Wright / GI-1.0 | World/hash for persistence | Treating gather cost as probe-pool cost |

---

## Articles (digests)

### Sparse 3D Radiance Cascades — Sannikov (2025)

- [YouTube demo](https://www.youtube.com/watch?v=TGLAxW0xVDU)
- Classic full-3D world RC with **sparse GPU hashmap**: only visible probes inserted.
- World resolution chosen so projected tile area is ~constant → **memory and work stay bounded** as the camera moves.
- **ngame:** absolute world keys; culled-surface seeding.

### Split Radiance Cascades — Freeman & Sannikov (2026)

- [arXiv:2607.20384](https://arxiv.org/abs/2607.20384) · [PDF](https://arxiv.org/pdf/2607.20384)
- Sparse hashmap world probes + **ray splitting** (hit distance → cascade intervals).
- LOD so on-screen probe footprint stays roughly constant.
- **ngame:** hash spine shipping; ray-split optional later for fill cost.

### Radiance Cascades hub

- [radiance.wiki](https://radiance.wiki/) — penumbra hypothesis, cascade merge, 2D/SS variants.
- [jason.today/rc](https://jason.today/rc) — interactive fundamentals.

### Lumen — Wright et al. (Epic)

- [Lumen SIGGRAPH 2022 PDF](https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Lumen-Wright-et-al.pdf)
- **Surface cache:** always-resident low-res pages + on-demand high-res from **GPU feedback**.
- **ngame:** stable world keys for static; partial rekey for movers; binary-split base leaves then refine.

### Nanite — Karis (Epic, SIGGRAPH 2021)

- [Nanite PDF](https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf)
- Retained GPU scene; **GPU cull + LOD cut**.
- **ngame:** BVH frustum cull → surface-hash leaves (split base).

### DDGI / irradiance fields

- Fixed probe counts; scrolling volumes invalidate edges.
- **ngame:** reject clip-scroll as product coverage; prefer world-hash surface cells.

---

## Anti-patterns (from literature vs our hot path)

| Bad | Why | Prefer |
|-----|-----|--------|
| Any hot-path `LoadImageFromTexture` | Sync stall | Keep vis/keep on GPU; VS / prev-frame consumers |
| CPU draw filter from vis bits | Needs readback | VS sample `tex_inst_vis` |
| Per-frame probe wipe | Wastes stable slots | Fingerprint skip + evict/insert |
| Placement cost ∝ resolution | Violates discrete-probe budget | Deduped keys; work ∝ `slot_cap` |
| Full-clip always-cover | Fights camera motion; not surface-matched | Culled-surface cover → gen waves |
| Look-at clip cube as GI volume | Artificial root; unstable for static cache | Absolute world keys |
| Fill when dirty==0 | Wasted atlas marches | Skip fill until resolve returns |

---

## Mapping to ngame phases

| Phase | State |
|-------|--------|
| GPU cull + VS draw cull (no vis readback) | **shipping** |
| Prev-vis probes + fingerprint + persistent slots | **shipping** |
| Gbuf after probes; fill / resolve | **later** |
| B.3–B.5 hash spine | retained as storage |
| B.6–B.6.6 clip always-cover | **deprecated** |

**Ship path:** GPU frustum cull → VS cull draw → prev-vis probes → world hash → gbuf; refine/fill later.

<!-- agent: composer-2.5 | 2026-08-11 | B62 GPU prio article map | 75bc1b -->
<!-- agent: composer-2.5 | 2026-08-11 | B63 Nanite cut cover article | 2b8343 -->
<!-- agent: composer-2.5 | 2026-08-11 | B64 Lumen K-list article | 6d0fdc -->
<!-- agent: composer-2.5 | 2026-08-11 | B65 independent steal article | 5c4188 -->
<!-- agent: composer-2.5 | 2026-08-12 | article GPU hot path vis bridge | 23a111 -->
<!-- agent: composer-2.5 | 2026-08-11 | B66 always-cover collapse article | 2aaa29 -->
<!-- agent: composer-2.5 | 2026-08-11 | B66 want-have balanced article | 5928ce -->
<!-- agent: composer-2.5 | 2026-08-11 | article deprecate clip always-cover | b04b5b -->
<!-- agent: composer-2.5 | 2026-08-12 | article zero readback | e6fdf1 -->
