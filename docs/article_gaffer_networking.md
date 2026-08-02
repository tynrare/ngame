<!-- agent: composer-2.5 | 2026-07-31 | phase frame article rewrite | 7e8046 -->
<!-- agent: composer-2.5 | 2026-07-31 | synctest lite backlog note | 69e975 -->
<!-- agent: composer-2.5 | 2026-07-31 | loss soak backlog note | ddf401 -->
<!-- agent: composer-2.5 | 2026-07-31 | adapt playout backlog note | bc3e78 -->
<!-- agent: composer-2.5 | 2026-08-01 | poor peer next backlog | 81f4a3 -->
<!-- agent: composer-2.5 | 2026-08-01 | poor peer backlog done note | 53724f -->
<!-- agent: composer-2.5 | 2026-08-01 | deeper predict backlog note | d461fd -->
<!-- agent: composer-2.5 | 2026-08-01 | hybrid mode article split | 5ddfed -->
<!-- agent: composer-2.5 | 2026-08-01 | server gaffer article rewrite | 4e35ee -->
<!-- agent: composer-2.5 | 2026-08-01 | docs lockstep js actions | 106d0d -->
# Networked physics & lockstep (ngame notes)

Pair with [`docs/scenes.md`](scenes.md), `src/scene/lockstep.c`, `src/scene/graph.c`, `src/net/mod_net.c`.

---

## Modes (ngame)

| `sim` | Literature home | Example scenes | Wire truth |
|-------|-----------------|----------------|------------|
| `"lockstep"` | [Gaffer #1](https://gafferongames.com/post/deterministic_lockstep/) classic wait-for-all | `lockstep.js` | Inputs + confirm when `all_have`; no ZF / predict / ghost |
| `"hybrid"` | Gaffer #1 + GGPO / SnapNet / Factorio | `physics.js`, `solar.js` | Confirm + ZF + predict + adapt + ghost + soft PHYS |
| omit / server | [Gaffer #2](https://gafferongames.com/post/snapshot_interpolation/)–[#3](https://gafferongames.com/post/state_synchronization/) (+ [Gambetta](https://www.gabrielgambetta.com/client-server-game-architecture.html) for owner) | `cube`, `sphere` | Host Box3D + unreliable `STATE_UPDATE`; Hermite view |

SESSION `lockstep` byte: `0=off, 1=pure, 2=hybrid` (proto ≥11). Current wire: `NG_PROTO_VERSION` **13** (smallest-three quat on STATE; lockstep action blobs).

---

## Implemented matrix (honest)

| Feature | lockstep | hybrid | server |
|---------|----------|--------|--------|
| UDP inputs + playout | yes | yes | n/a |
| Host confirm | yes (`all_have`) | yes (+ ZF deadline) | n/a |
| Mirror predict + Save ring | no | yes | n/a |
| Ghost / soft PHYS / adapt playout | no | yes | n/a |
| Hermite sample ring | n/a | n/a (bodies) | yes (adaptive delay) |
| Kinematic proxy + pose/vel drive | n/a | n/a | yes |
| Per-peer ACK delta (`NG_SYNC_SERVER`) | n/a | n/a | yes (shared stays absolute) |
| Smallest-three quat on STATE | n/a | n/a | yes |
| Hold tip (no naive extrapolate) | n/a | n/a | yes |
| Distance interest + send cap 24 | n/a | n/a | yes |
| Owner reconcile (small-error skip) | n/a | n/a | yes (minimal) |

---

## May also implement later

- Full dual-dynamic client state sync + soft correction springs (true Gaffer #3 pops)
- Arithmetic / context coding; relative index encodings at large entity counts
- Per-peer interest origin; PVS / streaming volumes
- Hitscan server rewind ([Bernier](https://www.gamedevs.org/uploads/latency-compensation-in-client-server-protocols.pdf)) — FPS server scenes only
- Factorio [FFF-83](https://www.factorio.com/blog/post/fff-83) latency-state UI (hybrid feel)
- Raise `NG_LOCK_PEER_MAX` / PHYS cost under large hybrid worlds
- Physics-aware extrapolation (Gaffer warns plain extrapolate for colliding rigid bodies)

---

## North star and phases

| Goal | Meaning |
|------|---------|
| **Stable** | Peers agree; desync heals; one lagging peer does not freeze everyone |
| **Authoritative server** | Host owns confirm timeline, zero-fill, prune, soft PHYS, late-join |
| **Deterministic physics** | Box3D XP/MT determinism + same confirmed inputs + fixed Δt |
| **Entity-mass** | Bandwidth ∝ inputs (and rare PHYS), not object count |
| **Fast-paced feel** | Tight controls — **later** (shallow predict only in hybrid) |
| **Large peer count** | Beyond small sessions — **later** |

**Phase 1 (current, accepted):** small-peer (`NG_LOCK_PEER_MAX` 8) host-confirmed **hybrid** on Box3D (`sim: "hybrid"`). Classic Gaffer wait-for-all is `sim: "lockstep"`. Server-mode Gaffer harden (delta / quat / adaptive interp / interest / owner reconcile) is shipped minimal — see matrix above.

**Closest goal next:** Factorio-style latency-state (if hybrid feel still lacks), then peer scale. Further server scale = “May also implement.”

Feel rule (input-sim): determinism **and** smoothness — buy smoothness with playout + shallow predict + rare PHYS, not by abandoning the confirmed timeline.

---

## Phase 1 shipped (input-sim)

### Model

```text
peers → LOCK_INPUT (UDP redundant; bits + optional POD action)
     → host LOCK_CONFIRM (reliable + hist; zeros after deadline)  [hybrid]
     → mirrors may predict ≤ PREDICT_MAX (last-input hold; bits only) [hybrid]
     → mispredict: local b3World_Save restore + resim            [hybrid]
     → LOCK_HASH streak / lag: soft LOCK_PHYS to that peer only  [hybrid]
     → silent: prune + roster RESUME
late join: PAUSE → PHYS → READY → RESUME                         [both]
```

JS sim actions: `action_register` / `action(name, …floats)` enqueue on the proposing peer; confirm carries the blob; all heaps `pcall_method` before `fixed_step`. Not camera-in-sim.
Two scene modes share the input-sim family: `sim: "lockstep"` (classic) and `sim: "hybrid"` (Phase 1 feel).

| | `sim: "lockstep"` (classic) | `sim: "hybrid"` (Phase 1) |
|--|-----------------------------|---------------------------|
| Step when | `all_have` → confirm | Host `LOCK_CONFIRM` |
| Missing input | STALL all | Zeros in confirm after deadline |
| Prediction | None | Last-input ≤ 9 (budget); Save ring |
| Hash / lag diverge | Hitch | Soft PHYS (one peer); silent prune / ghost |
| Confirm wire | Reliable when ready | Reliable + unsent hist + ZF |

**Box3D:** cross-platform + multi-thread determinism by design. `b3World_Save` / `Restore` = full seed snapshot (contacts, warm-start, islands) for PHYS and predict ring — **not** pose-only teleports, **not** `b3RecPlayer` for live net.

Gate **STALL** = `syncing` / `await_phys` / empty roster BUFFER — **not** permanent hash freeze.

### Knobs

| Knob | Value | Role |
|------|-------|------|
| `NG_LOCK_PEER_MAX` | 8 | Phase 1 peer cap |
| `NG_LOCK_PLAYOUT_TICKS` | 6 (~100 ms @ 60 Hz) | Send-ahead / jitter buffer |
| `NG_LOCK_CONFIRM_SEC` | 0.35 | Missing input → zeros in confirm |
| `NG_LOCK_PREDICT_MAX` | 9 (~150 ms @ 60 Hz) | Mirror predict past confirmed (hard max) |
| `NG_PHYS_SAVE_RING` | 12 (≥ predict+2) | Local rollback snapshots |
| `NG_LOCK_DESYNC_PHYS_STREAK` | 2 | Hash mismatches → soft PHYS |
| `NG_LOCK_CATCHUP_TICKS` | 45 | Lag behind confirmed → PHYS |
| `NG_LOCK_SILENT_SEC` | 1.0 | No heartbeat → prune |
| `NG_LOCK_CONFIRM_HIST` | 48 | Unsent confirm flush depth |
| `NG_MOD_FIXED_MAX_STEPS` | 4 | Catch-up cap / frame |
| `NG_STATE_INTERP_MS` | env | Override server Hermite delay (ms) |

### Recovery ladder

```text
confirm input mismatch → Restore save@(confirm-1) + resim
                      → missing save / fail → await_phys (mirror)
LOCK_HASH ≠ host      → streak (DESYNC_PHYS_STREAK=2)
                      → soft LOCK_PHYS @ confirmed + world_hash
                      → import + re-hash verify → soft snap clock
                      → retry ≤2 → ERROR (no freeze-all)
lag ≥ CATCHUP_TICKS   → same soft PHYS path
late join             → PAUSE → PHYS → READY → RESUME
```

### Map to code

| Concept | Where |
|---------|--------|
| `sim: "lockstep"` / `"hybrid"` / server | [`docs/scenes.md`](scenes.md); SESSION mode byte |
| Gate / confirm / predict | `src/scene/lockstep.c` (`mod_lockstep_is_hybrid`) |
| STATE delta / interest / Hermite | `src/scene/graph.c`, `src/net/mod_net.c` |
| Wire | ch0 inputs+STATE / ch1 SESSION·PHYS·RESUME·CONFIRM |
| Save / checksum | `src/scene/physics.c` — `b3World_Save`, Save ring |
| Proto | `NG_PROTO_VERSION` 13 |

### Tradeoffs (Phase 1 input-sim)

| Pressure | Phase 1 response |
|----------|------------------|
| Ping / RTT | Playout + confirm; predict ≤9 (budget shrinks if playout high) |
| Jitter | Fixed playout; BUFFER while filling |
| Loss | UDP input window; reliable CONFIRM hist |
| One lag spike | Confirm zeros; catchup PHYS; silent prune |
| CPU spiral | Max 4 fixed steps / frame |
| Bandwidth | Inputs + rare PHYS to targets only |

---

## Server mode (Gaffer #2/#3 family)

```text
host Box3D → STATE_UPDATE (unreliable, pose+vel, quat ST)
          → per-peer ACK delta for sync:server (keyframes ~30)
          → views: sample ring + adaptive Hermite (hold tip)
          → kinematic proxy driven with transform+vel
          → interest cull (R≈40, skip >2R); flush ≤24
owner (controller): local sim; skip apply if error < ~5cm/5°
```

Shared / multi-author entities stay **absolute** on the wire (delta broke multi-author seq namespaces).

Soak: `scripts/server_state_soak.sh`.

---

## Why this model (literature → choice)

| Source idea | ngame choice |
|-------------|--------------|
| Gaffer #1: playout, UDP redundant inputs, catch-up cap | `PLAYOUT` 6, `LOCK_INPUT` window, `FIXED_MAX_STEPS` 4 |
| Factorio: host merges tick; omit lagging player | Host `LOCK_CONFIRM`; zeros / prune isolate spikes |
| GGPO: last-input predict + Save/load/resim | Predict ≤9 + `b3World_Save` ring (not full GGPO) |
| Klotho: hash → rollback → full state | Hash streak → soft PHYS; Save resim first |
| Box3D: XP/MT determinism + full snapshot | Engine trust; Save for PHYS/predict — not recording player |
| Gaffer #2 Hermite + delay | Server sample ring; adaptive delay ≈ 3× arrival EMA |
| Gaffer compression / delta | Smallest-three quat; per-peer ACK delta for `sync:server` |
| Gaffer #3 priority subset | Interest + \|v\| priority; kinematic proxy (not dual dynamic) |
| Gambetta reconcile | Owner small-error skip under server sim |

FPS toolkits (Valve / Bernier) = lag-comp contrast; lockstep scenes do not rewind Box3D for hitscan fairness.

---

## Closest goal and further steps

**Next (closest):** Factorio-style latency-state (if hybrid feel still lacks), then peer scale.

| Order | Step | Cue |
|-------|------|-----|
| 1 | **Poor peer — done** | FFF-147; ghost rebind |
| 2 | **Deeper predict — done** | SnapNet delay-first |
| 3 | **Server Gaffer harden — done (minimal):** per-peer delta, ST quat, adaptive Hermite, interest, owner reconcile; soak `server_state_soak.sh` | Gaffer #2–#4 |
| **4 (next)** | Raise peer cap; PHYS cost — *or* FFF-83 latency-state | SnapNet 3+; FFF-83 |
| 5 | Later server scale (dual-dynamic #3, PVS, arithmetic coding) | See “May also” |

**Done (harden):** SyncTest-lite + loss soak; adaptive playout; poor-peer; deeper predict; lockstep/hybrid split; server STATE path minimal.

<!-- agent: composer-2.5 | 2026-07-31 | synctest lite backlog note | 69e975 -->
<!-- agent: composer-2.5 | 2026-07-31 | loss soak backlog note | ddf401 -->
<!-- agent: composer-2.5 | 2026-07-31 | adapt playout backlog note | bc3e78 -->
<!-- agent: composer-2.5 | 2026-08-01 | poor peer next backlog | 81f4a3 -->
<!-- agent: composer-2.5 | 2026-08-01 | poor peer backlog done note | 53724f -->
<!-- agent: composer-2.5 | 2026-08-01 | deeper predict backlog note | d461fd -->
<!-- agent: composer-2.5 | 2026-08-01 | server gaffer article rewrite | 4e35ee -->
Do not “optimize latency” by undoing Phase 1 anti-drift below.

---

## Anti-drift (Phase 1)

1. Playout ≥ ~6 @ 60 Hz — lag for smoothness.
2. Mirrors step on host `LOCK_CONFIRM`; zeros only inside CONFIRM (hybrid).
3. `LOCK_INPUT` unreliable/redundant; CONFIRM/SESSION/PHYS/RESUME reliable.
4. Soft PHYS / join PHYS → **target peer(s) only** — never fanout healthy peers.
5. Catch-up ≤ 4 fixed steps / frame.
6. Smooth ≠ lagless — do not break determinism for snappier keys.
7. Local Save ring for mispredict; net PHYS for join / lag / hash heal.
8. CONFIRM reliable + unsent hist (no skipped ticks on multi-step frames).
9. Trust Box3D XP determinism: `-ffp-contract=off`, no fast-math, matching layout/SIMD for Save blobs.
10. Save only at step boundaries on unlocked worlds; never `b3RecPlayer` as net heal.
11. Silent peer: zeros → catchup PHYS → hard prune; mirror net-loss clears roster.
12. Phase 1 keeps low peer cap — do not pretend MMO scale without backlog #4.
13. Long blackout (~tens of seconds): ghost/`NG_LOCK_PRUNE_SEC` + name rebind + soft PHYS — do not stretch playout/predict to cover it.
14. Prefer playout before deep predict (`predict_allow`); do not raise speculation past SnapNet ~150ms sports ceiling without more delay.
15. Server STATE: shared stays absolute; delta only for host-authored `sync:server` vs per-peer ACK.
16. Entity identity (input-sim): start/join use low ids (SESSION); action apply uses sim-band ids `f(tick,peer,seq)` identical on every heap; `sync:local` uses a private high band. Refuse net `spawn` outside start / action apply / join materialize.
17. Dual heap may mirror poses by **entity id** once sim-band ids agree — do not invent string keys for lockstep creates. PHYS body wire name is `e<id>/<desc>` (desc for soft-PHYS upsert); import rebinds/upserts/despawns by that name.
18. Predict / ZF must not invent action oneshots; re-apply of the same action is idempotent (same sim id). View-only action propose when dual heaps are loaded.
<!-- agent: composer-2.5 | 2026-08-02 | article sim entity phys upsert | 6bee57 -->

<!-- agent: composer-2.5 | 2026-08-01 | docs entity identity scopes | d4b0d1 -->

### Entity identity (Phase 1 actions)

JS `action(...)` propose → confirm blob → `action_fire` on every loaded heap before `fixed_step`. Spawns during apply receive deterministic sim-band entity ids (no per-heap `alloc_id`, no required spawn key). View draw matches server poses by id. Server-sim role-gated create + STATE unchanged (Gaffer #2–#3).

---

## Appendix: curated reading

Skim: Box3D → lockstep core → auth → predict/recover → snapshot/state sync → FPS contrast.

### Physics (Box3D)

| Source | Summary |
|--------|---------|
| [erincatto/box3d](https://github.com/erincatto/box3d) / [Announcing Box3D](https://box2d.org/posts/2026/06/announcing-box3d/) | C17 3D engine; XP determinism; recording/replay. |
| [Determinism (Box2D/3D)](https://box2d.org/posts/2024/08/determinism/) | Algorithmic + MT + cross-platform; pose-only rollback insufficient. |
| In-tree `third_party/box3d/docs/simulation.md`, `faq.md`, `recording.md` | Save = recording seed snapshot; player for debug/CI. |
| [Jolt Deterministic Simulation](https://jrouwe.github.io/JoltPhysics/#deterministic-simulation) | Peer engine contrast; ngame stays on Box3D. |

### Lockstep core

| Source | Summary |
|--------|---------|
| [Gaffer — Intro](https://gafferongames.com/post/introduction_to_networked_physics/) | Contact-coupled rigid bodies = hard case. |
| [Gaffer — Deterministic Lockstep](https://gafferongames.com/post/deterministic_lockstep/) | Inputs only; playout; UDP redundancy; hitch if input *n* missing. |
| [Gaffer — Floating Point Determinism](https://gafferongames.com/post/floating_point_determinism/) | Historical float traps; app/build still matter. |
| [1500 Archers](https://www.gamedeveloper.com/programming/1500-archers-on-a-28-8-network-programming-in-age-of-empires-and-beyond) ([PDF](https://zoo.cs.yale.edu/classes/cs538/readings/papers/terrano_1500arch.pdf)) | Command lockstep; turn delay; entity-mass via inputs. |
| [SnapNet — Lockstep](https://www.snapnet.dev/blog/netcode-architectures-part-1-lockstep/) | Wait-for-all → delay ∝ worst peer; late join. |

### Snapshot / state sync (server mode)

| Source | Summary |
|--------|---------|
| [Gaffer — Snapshot Interpolation](https://gafferongames.com/post/snapshot_interpolation/) | Buffer + Hermite; skip lost snapshots; extrapolate weak for RB. |
| [Gaffer — Snapshot Compression](https://gafferongames.com/post/snapshot_compression/) | Quantize; at-rest; ACK delta; smallest-three quat. |
| [Gaffer — State Synchronization](https://gafferongames.com/post/state_synchronization/) | Client sims + sparse state; priority subset. |

### Auth / isolate lag

| Source | Summary |
|--------|---------|
| [Factorio FFF-147](https://www.factorio.com/blog/post/fff-147) | Host merges actions; can omit lagging client. |
| [Factorio FFF-76](https://www.factorio.com/blog/post/fff-76) / [FFF-83](https://www.factorio.com/blog/post/fff-83) | Huge entity counts; latency-state UI. |
| [mas-bandwidth — network model](https://mas-bandwidth.com/choosing-the-right-network-model-for-your-multiplayer-game/) | Lockstep vs Quake/GGPO genres. |

### Predict / recover

| Source | Summary |
|--------|---------|
| [GGPO](https://www.ggpo.net/) / [DeveloperGuide](https://github.com/pond3r/ggpo/blob/master/doc/DeveloperGuide.md) | Speculative advance; Save/load/step; SyncTest. |
| [GGPO lag-fighting](https://www.gamedeveloper.com/programming/the-lag-fighting-techniques-behind-ggpo-s-netcode) | Hide RTT on remote; keep local snappy. |
| [SnapNet — Rollback](https://www.snapnet.dev/blog/netcode-architectures-part-2-rollback/) / [Delay vs Rollback](https://www.snapnet.dev/docs/core-concepts/input-delay-vs-rollback/) | Mix input delay + predict. |
| [Klotho SynchronizationDesign](https://github.com/xpTURN/Klotho/blob/main/Docs/SynchronizationDesign.md) | Verified vs Predicted; recovery ladder. |

### Contrast (FPS / state auth)

| Source | Summary |
|--------|---------|
| [Gambetta FPM](https://www.gabrielgambetta.com/client-server-game-architecture.html) | Auth + prediction + reconciliation + interp. |
| [Valve Source networking](https://developer.valvesoftware.com/wiki/Source_Multiplayer_Networking) | Snapshots, tick, lag-comp vocabulary. |
| [Bernier latency compensation](https://www.gamedevs.org/uploads/latency-compensation-in-client-server-protocols.pdf) | Server rewind for hits. |
| [Factorio desync wiki](https://wiki.factorio.com/Desynchronization) | Dump culture for long sessions. |
| [GameNetworkingResources](https://github.com/petitgamedev/GameNetworkingResources) | Index of classics. |

### Three strategies (one-liners)

| Strategy | Wire | Bit-identical sim? | ngame home |
|----------|------|-------------------|------------|
| Deterministic lockstep | Inputs | **Yes** | `lockstep` / `hybrid` |
| Snapshot interpolation | Visual state | No | server Hermite path |
| State synchronization | Inputs + sparse state | Approximate | server priority + proxy (lite) |

<!-- agent: composer-2.5 | 2026-07-31 | phase frame article rewrite | 7e8046 -->
<!-- agent: composer-2.5 | 2026-07-31 | synctest lite backlog note | 69e975 -->
<!-- agent: composer-2.5 | 2026-07-31 | loss soak backlog note | ddf401 -->
<!-- agent: composer-2.5 | 2026-07-31 | adapt playout backlog note | bc3e78 -->
<!-- agent: composer-2.5 | 2026-08-01 | poor peer next backlog | 81f4a3 -->
<!-- agent: composer-2.5 | 2026-08-01 | poor peer backlog done note | 53724f -->
<!-- agent: composer-2.5 | 2026-08-01 | deeper predict backlog note | d461fd -->
<!-- agent: composer-2.5 | 2026-08-01 | hybrid mode article split | 5ddfed -->
<!-- agent: composer-2.5 | 2026-08-01 | server gaffer article rewrite | 4e35ee -->
<!-- agent: composer-2.5 | 2026-08-01 | docs lockstep js actions | 106d0d -->
<!-- agent: composer-2.5 | 2026-08-01 | docs entity identity scopes | d4b0d1 -->
<!-- agent: composer-2.5 | 2026-08-02 | article sim entity phys upsert | 6bee57 -->
