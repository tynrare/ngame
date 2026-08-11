<!-- agent: composer-2.5 | 2026-08-11 | B62 GPU prio article map | 75bc1b -->
<!-- agent: composer-2.5 | 2026-08-11 | B63 Nanite cut cover article | 2b8343 -->
# Sparse probes & GPU feedback (ngame notes)

Pair with [`docs/radiance-cascades-3d.md`](radiance-cascades-3d.md), `src/client/render_rc_ws.c`.

Performance north star: **probe work ∝ unique slots**, not screen pixels. Screen scales only lighting raster / resolve. No CPU-heavy placement if GPU can do it.

---

## Takeaways for Track B

| Source | What we copy | What we reject |
|--------|--------------|----------------|
| Sparse 3D RC / Split RC | GPU hashmap; projected-size LOD → ~constant tile count; near surfaces only | Per-frame CPU depth readback seed |
| Lumen | GPU feedback → compact → tiny request list; near steals pages; coarse always resident | Full CPU hierarchy walks on hot path |
| Nanite | Retained structure; GPU cull/LOD cut; CPU streams from feedback only | Resolution-tied placement cost |
| DDGI / cascaded volumes | Fixed probe budget; scroll invalidate edges; GPU blend | Dense N³ as long-term default |
| Wright / GI-1.0 | Screen probes for gather detail; world/hash for persistence | Treating gather cost as probe-pool cost |

---

## Articles (digests)

### Sparse 3D Radiance Cascades — Sannikov (2025)

- [YouTube demo](https://www.youtube.com/watch?v=TGLAxW0xVDU)
- Classic full-3D world RC with **sparse GPU hashmap**: only visible probes inserted.
- World resolution chosen so projected tile area is ~constant → **memory and work stay bounded** as the camera moves.
- **ngame:** same bound — unique keys, not seed-pixel loops.

### Split Radiance Cascades — Freeman & Sannikov (2026)

- [arXiv:2607.20384](https://arxiv.org/abs/2607.20384) · [PDF](https://arxiv.org/pdf/2607.20384)
- Sparse hashmap world probes + **ray splitting** (hit distance → cascade intervals).
- LOD so on-screen probe footprint stays roughly constant.
- **ngame:** B.3–B.5 storage spine; ray-split optional later for fill cost.

### Radiance Cascades hub

- [radiance.wiki](https://radiance.wiki/) — penumbra hypothesis, cascade merge, 2D/SS variants.
- [jason.today/rc](https://jason.today/rc) — interactive fundamentals.

### Lumen — Wright et al. (Epic)

- [Lumen SIGGRAPH 2022 PDF](https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Lumen-Wright%20et%20al.pdf)
- **Surface cache:** always-resident low-res pages + on-demand high-res from **GPU feedback** (hit → hash compact → small download → map/unmap).
- Allocate by distance/size/hit importance; deallocate under memory pressure.
- **ngame B.6.2:** score cache → top-K list (≤8) → apply; near steals via soft 25% reserve.

### Nanite — Karis (Epic, SIGGRAPH 2021)

- [Nanite PDF](https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf)
- Retained GPU scene; **GPU cull + LOD cut**; CPU loads from feedback.
- **ngame:** coarse leaves resident; near demand splits by stealing far (relax scores).

### DDGI / irradiance fields

- Fixed probe counts; GPU ray + blend; cascaded / scrolling volumes.
- **ngame:** quality knobs = slot/dir budgets; self-bias in resolve.

---

## Anti-patterns (from literature vs our hot path)

| Bad | Why | Prefer |
|-----|-----|--------|
| `LoadImageFromTexture` seed every frame | Sync stall; CPU O(seed) | Score ∝ slots; tiny K list only |
| Placement cost ∝ resolution | Violates discrete-probe budget | Deduped keys; work ∝ `slot_cap` |
| Hard `reserve+N` promote gates | Deadlock; near starves | Soft reserve; steal then promote |
| Multi-scan CPU if/else policy | Heavier than dense N³ fill | One score pass + top-K |
| Fill when dirty==0 | Wasted atlas marches | Skip fill; resolve only |

---

## Mapping to ngame phases

| Phase | Literature alignment |
|-------|----------------------|
| B.3–B.5 | Sparse keys + covering resolve |
| B.6–B.6.4 | Interim (superseded) |
| B.6.5 | **superseded** — frustum evict left holes |
| **B.6.6 (shipping)** | Always-cover; want/have promote; dispose ∝ fine depth; interleaved K |
| Later | GPU `tex_ops` readback; full GPU apply |

**Ship path:** full-clip cover; coarsen over-fine/far; split only too-coarse; one score; apply ≤K; idle = zero.

<!-- agent: composer-2.5 | 2026-08-11 | B62 GPU prio article map | 75bc1b -->
<!-- agent: composer-2.5 | 2026-08-11 | B63 Nanite cut cover article | 2b8343 -->
<!-- agent: composer-2.5 | 2026-08-11 | B64 Lumen K-list article | 6d0fdc -->
<!-- agent: composer-2.5 | 2026-08-11 | B65 independent steal article | 5c4188 -->
<!-- agent: composer-2.5 | 2026-08-11 | B66 always-cover collapse article | 2aaa29 -->
<!-- agent: composer-2.5 | 2026-08-11 | B66 want-have balanced article | 5928ce -->
