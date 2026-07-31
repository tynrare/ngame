<!-- agent: composer-2.5 | 2026-07-31 | phase frame article rewrite | 7e8046 -->
# Networked physics & lockstep (ngame notes)

Pair with [`docs/scenes.md`](scenes.md), `src/scene/lockstep.c`, `src/net/mod_net.c`.

---

## North star and phases

| Goal | Meaning |
|------|---------|
| **Stable** | Peers agree; desync heals; one lagging peer does not freeze everyone |
| **Authoritative server** | Host owns confirm timeline, zero-fill, prune, soft PHYS, late-join |
| **Deterministic physics** | Box3D XP/MT determinism + same confirmed inputs + fixed Δt |
| **Entity-mass** | Bandwidth ∝ inputs (and rare PHYS), not object count |
| **Fast-paced feel** | Tight controls — **later** (shallow predict only in Phase 1) |
| **Large peer count** | Beyond small sessions — **later** |

**Phase 1 (current, accepted):** small-peer (`NG_LOCK_PEER_MAX` 8) host-confirmed lockstep on Box3D. LAN-stable agreement + smoothness without freeze-all. Not fighter-snappy GGPO; not MMO peer scale.

**Closest goal next:** harden Phase 1 (tests/dumps, loss stress at 2–8 peers), then feel polish, then peer scale.

Feel rule: determinism **and** smoothness — buy smoothness with playout + shallow predict + rare PHYS, not by abandoning the confirmed timeline.

---

## Phase 1 shipped

### Model

```text
peers → LOCK_INPUT (UDP redundant)
     → host LOCK_CONFIRM (reliable + hist; zeros after deadline)
     → mirrors may predict ≤ PREDICT_MAX (last-input hold)
     → mispredict: local b3World_Save restore + resim
     → LOCK_HASH streak / lag: soft LOCK_PHYS to that peer only
     → silent: prune + roster RESUME
late join: PAUSE → PHYS → READY → RESUME
```

One code path for multi-peer `sim: "lockstep"`. Classic Gaffer #1 waits on `all_have` (hitch everyone); ngame confirms zeros after `NG_LOCK_CONFIRM_SEC` so healthy peers keep moving.

| | Classic Gaffer #1 | Phase 1 ngame |
|--|-------------------|---------------|
| Step when | `all_have(next)` | Host `LOCK_CONFIRM` |
| Missing input | STALL all | Zeros in confirm after deadline |
| Prediction | None | Last-input ≤ 8; Save ring |
| Hash / lag diverge | Stuck / hitch | Soft PHYS (one peer); silent prune |
| Confirm wire | n/a | Reliable + unsent hist |

**Box3D:** cross-platform + multi-thread determinism by design. `b3World_Save` / `Restore` = full seed snapshot (contacts, warm-start, islands) for PHYS and predict ring — **not** pose-only teleports, **not** `b3RecPlayer` for live net.

Gate **STALL** = `syncing` / `await_phys` / empty roster BUFFER — **not** permanent hash freeze.

### Knobs

| Knob | Value | Role |
|------|-------|------|
| `NG_LOCK_PEER_MAX` | 8 | Phase 1 peer cap |
| `NG_LOCK_PLAYOUT_TICKS` | 6 (~100 ms @ 60 Hz) | Send-ahead / jitter buffer |
| `NG_LOCK_CONFIRM_SEC` | 0.35 | Missing input → zeros in confirm |
| `NG_LOCK_PREDICT_MAX` | 8 | Mirror predict past confirmed |
| `NG_PHYS_SAVE_RING` | 10 (≥ predict+2) | Local rollback snapshots |
| `NG_LOCK_DESYNC_PHYS_STREAK` | 2 | Hash mismatches → soft PHYS |
| `NG_LOCK_CATCHUP_TICKS` | 45 | Lag behind confirmed → PHYS |
| `NG_LOCK_SILENT_SEC` | 1.0 | No heartbeat → prune |
| `NG_LOCK_CONFIRM_HIST` | 48 | Unsent confirm flush depth |
| `NG_MOD_FIXED_MAX_STEPS` | 4 | Catch-up cap / frame |

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
| `sim: "lockstep"` | [`docs/scenes.md`](scenes.md) |
| Gate / confirm / predict | `src/scene/lockstep.c`, `lockstep.h` |
| Wire | `src/net/mod_net.c` — ch0 inputs / ch1 SESSION·PHYS·RESUME·CONFIRM |
| Save / checksum | `src/scene/physics.c` — `b3World_Save`, Save ring |
| Proto | `NG_PROTO_VERSION` 9 (`world_hash` on PHYS) |

Non-lockstep scenes (`cube` / `sphere`): pose/vel streams (Gaffer #2/#4 family) — not this path.

### Tradeoffs (Phase 1)

| Pressure | Phase 1 response |
|----------|------------------|
| Ping / RTT | Playout + confirm; predict ≤8 |
| Jitter | Fixed playout; BUFFER while filling |
| Loss | UDP input window; reliable CONFIRM hist |
| One lag spike | Confirm zeros; catchup PHYS; silent prune |
| CPU spiral | Max 4 fixed steps / frame |
| Bandwidth | Inputs + rare PHYS to targets only |

---

## Why this model (literature → choice)

| Source idea | Phase 1 choice |
|-------------|----------------|
| Gaffer #1: playout, UDP redundant inputs, catch-up cap | `PLAYOUT` 6, `LOCK_INPUT` window, `FIXED_MAX_STEPS` 4 |
| Factorio: host merges tick; omit lagging player | Host `LOCK_CONFIRM`; zeros / prune isolate spikes |
| GGPO: last-input predict + Save/load/resim | Predict ≤8 + `b3World_Save` ring (not full GGPO) |
| Klotho: hash → rollback → full state | Hash streak → soft PHYS; Save resim first |
| Box3D: XP/MT determinism + full snapshot | Engine trust; Save for PHYS/predict — not recording player |

FPS toolkits (Gambetta / Valve / Bernier) = state prediction / lag-comp — contrast only; lockstep scenes do not rewind Box3D for hitscan fairness.

---

## Closest goal and further steps

**Next (closest):** keep low peers; make Phase 1 boringly reliable.

| Order | Step | Cue |
|-------|------|-----|
| 1 | SyncTest / hash desync dumps; loss+jitter stress @ 2–8 peers | GGPO SyncTest; Factorio desync wiki |
| 2 | Adaptive or per-peer latency (still one confirm authority) | Factorio FFF-147; SnapNet delay mix |
| 3 | Deeper predict and/or Factorio-style latency-state for feel | GGPO; FFF-83 |
| 4 | Raise peer cap; PHYS cost under large worlds | SnapNet 3+; mas-bandwidth |
| 5 | Gaffer #2/#3 as primary only if a scene leaves lockstep | Snapshot / compression series |

Do not “optimize latency” by undoing Phase 1 anti-drift below.

---

## Anti-drift (Phase 1)

1. Playout ≥ ~6 @ 60 Hz — lag for smoothness.
2. Mirrors step on host `LOCK_CONFIRM`; zeros only inside CONFIRM.
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

---

## Appendix: curated reading

Skim: Box3D → lockstep core → auth → predict/recover → FPS contrast.

### Physics (Box3D)

| Source | Summary |
|--------|---------|
| [erincatto/box3d](https://github.com/erincatto/box3d) / [Announcing Box3D](https://box2d.org/posts/2026/06/announcing-box3d/) | C17 3D engine; XP determinism; recording/replay; noted for large auth sims (incl. 1000-player space game mention). |
| [Determinism (Box2D/3D)](https://box2d.org/posts/2024/08/determinism/) | Algorithmic + MT + cross-platform by default; no fast-math / FMA; custom trig; pose-only rollback insufficient. |
| In-tree `third_party/box3d/docs/simulation.md`, `faq.md`, `recording.md` | Same contract; Save = recording seed snapshot; player for debug/CI. |
| [Jolt Deterministic Simulation](https://jrouwe.github.io/JoltPhysics/#deterministic-simulation) | Peer engine + StateRecorder contrast; ngame stays on Box3D. |

### Lockstep core

| Source | Summary |
|--------|---------|
| [Gaffer — Deterministic Lockstep](https://gafferongames.com/post/deterministic_lockstep/) | Inputs only; playout; UDP redundancy; hitch if input *n* missing. |
| [Gaffer — Floating Point Determinism](https://gafferongames.com/post/floating_point_determinism/) | Historical float traps; app/build still matter. |
| [Gaffer — Intro](https://gafferongames.com/post/introduction_to_networked_physics/) | Contact-coupled rigid bodies = hard case. |
| [1500 Archers](https://www.gamedeveloper.com/programming/1500-archers-on-a-28-8-network-programming-in-age-of-empires-and-beyond) ([PDF](https://zoo.cs.yale.edu/classes/cs538/readings/papers/terrano_1500arch.pdf)) | Command lockstep; turn delay; speed control; entity-mass via inputs. |
| [SnapNet — Lockstep](https://www.snapnet.dev/blog/netcode-architectures-part-1-lockstep/) | Wait-for-all → delay ∝ worst peer; late join; 3+ players amplify bad links. |

### Auth / isolate lag

| Source | Summary |
|--------|---------|
| [Factorio FFF-147](https://www.factorio.com/blog/post/fff-147) | Host merges actions O(n); can omit lagging client from a tick. |
| [Factorio FFF-76](https://www.factorio.com/blog/post/fff-76) / [FFF-83](https://www.factorio.com/blog/post/fff-83) | Why lockstep for huge entity counts; latency-state UI over sacred sim. |
| [mas-bandwidth — network model](https://mas-bandwidth.com/choosing-the-right-network-model-for-your-multiplayer-game/) | Lockstep for high units / low players; Quake/GGPO for other genres. |

### Predict / recover

| Source | Summary |
|--------|---------|
| [GGPO](https://www.ggpo.net/) / [DeveloperGuide](https://github.com/pond3r/ggpo/blob/master/doc/DeveloperGuide.md) | Speculative advance; Save/load/step; SyncTest. |
| [GGPO lag-fighting](https://www.gamedeveloper.com/programming/the-lag-fighting-techniques-behind-ggpo-s-netcode) | Hide RTT on remote; keep local snappy. |
| [SnapNet — Rollback](https://www.snapnet.dev/blog/netcode-architectures-part-2-rollback/) / [Delay vs Rollback](https://www.snapnet.dev/docs/core-concepts/input-delay-vs-rollback/) | Mix input delay + predict. |
| [Klotho SynchronizationDesign](https://github.com/xpTURN/Klotho/blob/main/Docs/SynchronizationDesign.md) | Verified vs Predicted; recovery ladder. |

### Contrast (non-lockstep / FPS)

| Source | Summary |
|--------|---------|
| [Gaffer #2–#4](https://gafferongames.com/post/snapshot_interpolation/) | Snapshots, compression, state sync — ngame non-lockstep scenes. |
| [Gambetta FPM](https://www.gabrielgambetta.com/client-server-game-architecture.html) | Auth + prediction + reconciliation + interp (state truth). |
| [Valve Source networking](https://developer.valvesoftware.com/wiki/Source_Multiplayer_Networking) | Snapshots, tick, lag-comp vocabulary. |
| [Bernier latency compensation](https://www.gamedevs.org/uploads/latency-compensation-in-client-server-protocols.pdf) | Server rewind for hits — not ngame lockstep path. |
| [Factorio desync wiki](https://wiki.factorio.com/Desynchronization) | Dump culture for long sessions. |
| [GameNetworkingResources](https://github.com/petitgamedev/GameNetworkingResources) | Index of classics. |

### Three strategies (one-liners)

| Strategy | Wire | Bit-identical sim? |
|----------|------|-------------------|
| Deterministic lockstep | Inputs | **Yes** (Phase 1) |
| Snapshot interpolation | Visual state | No |
| State synchronization | Inputs + sparse state | Approximate |

<!-- agent: composer-2.5 | 2026-07-31 | phase frame article rewrite | 7e8046 -->
