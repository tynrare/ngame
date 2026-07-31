<!-- agent: composer-2.5 | 2026-07-31 | gaffer article ladder cleanup | 5bac62 -->
<!-- agent: composer-2.5 | 2026-07-31 | drop mode a/b labels | e8baf8 -->
<!-- agent: composer-2.5 | 2026-07-31 | curated net sources goals | c18378 -->
<!-- agent: composer-2.5 | 2026-07-31 | ngame goals tradeoffs frame | 853c5b -->
<!-- agent: composer-2.5 | 2026-07-31 | box3d determinism save notes | 56d64a -->
# Networked physics & lockstep (ngame notes)

Reference for agents and humans. Pair with [`docs/scenes.md`](scenes.md), `src/scene/lockstep.c`, `src/net/mod_net.c`.

---

## ngame goals (design north star)

| Goal | Meaning for netcode |
|------|---------------------|
| **Stable** | Peers stay in agreement; desync heals (hash → soft PHYS); silent/lagging peers don’t freeze everyone |
| **Fast-paced** | Controls feel tight; hitch budget small; limited predict + confirm deadline, not “wait forever for slowest” |
| **Mass multiplayer** | Bandwidth ∝ **inputs** (and occasional PHYS), not object count; host merges/confirms so fan-in is O(n), not O(n²) peer mesh |
| **Authoritative server** | Host owns confirm timeline, zero-fill, prune, soft PHYS, late-join PAUSE/PHYS/RESUME |
| **Deterministic physics** | **Box3D** is designed for cross-platform + multi-thread determinism (same inputs → same world). Host-confirmed inputs + fixed Δt; `LOCK_HASH` detects app-level drift; soft PHYS heals |

**Non-negotiable feel:** determinism across clients **and** smoothness/stability. Pure hitch-lockstep gives determinism but stalls; pure snapshot FPS gives smoothness without bit-identical physics. ngame’s path is **host-confirmed deterministic lockstep** on Box3D with light predict, Save-ring rollback, and graded state heal.

---

## Usual networking tradeoffs (ping · throttle · lag)

| Pressure | What it costs | How literature handles it | What ngame does today |
|----------|---------------|---------------------------|------------------------|
| **Ping / RTT** | Input→visible delay | Playout / input delay; Factorio per-client latency; GGPO hide remote lag | `NG_LOCK_PLAYOUT_TICKS` (~100 ms) + confirm; mirrors predict ≤8 |
| **Jitter** | Hitched steps if buffer empty | Playout / jitter buffer; adaptive delay | Fixed playout; BUFFER gate while filling |
| **Loss** | Missing input *n* | UDP redundant unacked inputs (Gaffer); Factorio resend merged tick | `LOCK_INPUT` unreliable + window; `LOCK_CONFIRM` reliable + hist |
| **Throttle / bandwidth** | Dropped or delayed packets under load | Prioritize inputs; compress snapshots elsewhere | Inputs small; PHYS only join/lag/hash targets — never fanout healthy peers |
| **Lag spike (one peer)** | Classic lockstep freezes **all** | Factorio omit lagging player from merged tick; SnapNet per-peer delay | Confirm deadline **zeros**; catchup PHYS; `NG_LOCK_SILENT_SEC` prune |
| **CPU / catch-up** | Spiral of death | Cap sim steps / frame (Gaffer: 4) | `NG_MOD_FIXED_MAX_STEPS` = 4 |
| **Deep predict** | Snappy feel, mispredict pops, CPU | GGPO / SnapNet rollback windows | Cap `NG_LOCK_PREDICT_MAX`; local Save restore — not full GGPO |

**Rule of thumb (Gaffer / SnapNet / Factorio agree):** smoothness is bought with **delay**, **prediction**, or **approximation**. ngame keeps bit-identical confirmed timeline and spends a little delay + shallow predict + rare PHYS.

---

## Sources → utilized → further

| Bucket | What | Links |
|--------|------|-------|
| **Primary (Gaffer)** | Networked Physics 0–4; overview; float determinism; new blog | [0 Intro](https://gafferongames.com/post/introduction_to_networked_physics/), [1 Lockstep](https://gafferongames.com/post/deterministic_lockstep/), [2 Snapshots](https://gafferongames.com/post/snapshot_interpolation/), [3 Compression](https://gafferongames.com/post/snapshot_compression/), [4 State sync](https://gafferongames.com/post/state_synchronization/), [What every programmer…](https://gafferongames.com/post/what_every_programmer_needs_to_know_about_game_networking/), [Floating-point determinism](https://gafferongames.com/post/floating_point_determinism/), [2004 physics](https://gafferongames.com/post/networked_physics_2004/), [Choose a network model](https://mas-bandwidth.com/choosing-the-right-network-model-for-your-multiplayer-game/), [mas-bandwidth.com](https://mas-bandwidth.com/) |
| **Physics (Box3D)** | Cross-platform / multi-thread determinism; world **Save/Restore** blobs (seed snapshot); recording/replay for debug — not the net path | [Box3D](https://github.com/erincatto/box3d), [Determinism (Box2D/3D lineage)](https://box2d.org/posts/2024/08/determinism/), [Recording](https://box2d.org/documentation3d/recording.html), in-tree `docs/` under `third_party/box3d/` |
| **Utilized (ngame lockstep)** | Fixed Δt + playout + UDP redundant `LOCK_INPUT`; host `LOCK_CONFIRM` + zero-fill; last-input predict ≤8; `b3World_Save` ring rollback; `LOCK_HASH` → soft PHYS; late-join PHYS | GGPO last-input / save-load; SnapNet lockstep→rollback; Klotho recovery ladder; Factorio host-merged inputs + isolate lag spikes |
| **Further (not in tree yet)** | SyncTest / desync dumps; SnapNet input-decay mix; Klotho rung-3 pause-all; deep predict ≫8; adaptive playout; Gaffer #2/#3 as primary path | See [Curated reading](#curated-reading-summaries) |

Non-lockstep scenes (`cube` / `sphere`) use pose/vel streams closer to Gaffer #2/#4 — not the confirm lockstep path.

---

## Curated reading (summaries)

Classic / high-signal pieces selected for ngame’s goals. Skim order: **Box3D** → lockstep core → auth server → predict/rollback → FPS contrast.

### Physics substrate (Box3D)

| Source | Summary |
|--------|---------|
| [erincatto/box3d](https://github.com/erincatto/box3d) / [Announcing Box3D](https://box2d.org/posts/2026/06/announcing-box3d/) | Portable C17 3D rigid-body engine (Box2D + Rubikon lineage). Features list **cross-platform determinism** and **recording/replay**. Used in Legend of California, s&box, Esoterica, and noted for a **1000-player** space game (Gaffer) — aligns with mass + auth + determinism goals. |
| [Box2D/3D — Determinism](https://box2d.org/posts/2024/08/determinism/) | Three levels shipped by default: algorithmic (no RNG), multithreaded (creation-order / bit-array merge, not race order), **cross-platform** (no fast-math, `-ffp-contract=off`, custom trig). Falling Hinges CI hash across MSVC/Clang/GCC × x64/ARM. **Pose/vel-only rollback is not enough** — engines cache warm-start/order; need full internal state or accept jitter. Fixed-point deliberately rejected. |
| In-tree `third_party/box3d/docs/simulation.md` + `faq.md` | Same story for Box3D: deterministic across thread counts and 64-bit platforms; app still must be deterministic. FAQ: no “rollback by teleporting bodies” guarantee — use Save image. |
| [Recording and Replay](https://box2d.org/documentation3d/recording.html) (+ in-tree `docs/recording.md`) | Seed **snapshot** (bodies, contacts+warm-start, islands, BP trees, geometry) + op log. Layout hash gates load. Debug/CI tool. **Do not** drive live net rollback with `b3RecPlayer`. |
| ngame `b3World_Save` / `b3World_Restore` | Exposed save **snippet**: same seed-snapshot framing as recording (step boundary, unlocked world). Used for `LOCK_PHYS` and local predict Save ring. `userData` cleared — rebind by body name. Matching layout/SIMD build required for blob load. |
| [Jolt — Deterministic Simulation](https://jrouwe.github.io/JoltPhysics/#deterministic-simulation) | Peer engine with `mDeterministicSimulation` + `StateRecorder` snapshots. Useful contrast for Save/restore culture; ngame stays on Box3D. |

### Lockstep & determinism (core)

| Source | Summary |
|--------|---------|
| [Gaffer — Deterministic Lockstep](https://gafferongames.com/post/deterministic_lockstep/) | Inputs only → bandwidth ∝ input size. Sample per-frame structs (not events). Cannot step *n* without input *n*. Playout buffer trades latency for smooth dequeue. UDP + redundant unacked window beats TCP under loss. Cap catch-up frames. Classic warning: float engines often aren’t XP-deterministic — **Box3D is** (see above). |
| [Gaffer — Floating Point Determinism](https://gafferongames.com/post/floating_point_determinism/) | Historical traps (x87, libm, FMA). Still required reading for *app* code and build flags. Box3D already applies the engine-side mitigations Erin documents. |
| [Gaffer — Intro to Networked Physics](https://gafferongames.com/post/introduction_to_networked_physics/) | Sets up the ODE cube/katamari demo: contact-coupled rigid bodies are the hard case all three strategies must face. |
| [Terrano/Bettner — 1500 Archers on a 28.8](https://www.gamedeveloper.com/programming/1500-archers-on-a-28-8-network-programming-in-age-of-empires-and-beyond) ([PDF](https://zoo.cs.yale.edu/classes/cs538/readings/papers/terrano_1500arch.pdf)) | Industry classic: command lockstep for massive unit counts. Turn delay (commands execute *N* turns later), speed control so the game runs as fast as the slowest peer, checksum desync detection. Shows why “mass entities” favors inputs-over-state. |
| [SnapNet — Netcode Architectures Part 1: Lockstep](https://www.snapnet.dev/blog/netcode-architectures-part-1-lockstep/) | Modern restatement: wait-for-all → input delay ∝ worst peer; determinism + fixed tick; late-join via state dump vs full input replay; 3+ players amplify bad connections. Client/server state-broadcast variant drops determinism but costs bandwidth. |

### Authoritative server + mass / isolate lag

| Source | Summary |
|--------|---------|
| [Factorio FFF-147 — Multiplayer rewrite](https://www.factorio.com/blog/post/fff-147) | Server unpacks/merges client actions into one tick package (O(n) not O(n²)). Lagging client can be **omitted** from a tick so others aren’t frozen — same spirit as ngame confirm zero-fill + prune. Per-player latency vs global. |
| [Factorio FFF-76 — MP inside out](https://www.factorio.com/blog/post/fff-76) | Why lockstep for huge entity counts; peer-to-peer era limits (slowest peer gates all); contrast with FPS client prediction. |
| [Factorio FFF-83 — Hide the latency](https://www.factorio.com/blog/post/fff-83) | “Latency state” UI/prediction layer over sacred deterministic game state — feel responsive without rolling back the whole factory sim. Useful when full GGPO resim is too expensive. |
| [mas-bandwidth — Choosing the right network model](https://mas-bandwidth.com/choosing-the-right-network-model-for-your-multiplayer-game/) | Decision checklist: player count, CPU for rollback, competitive vs coop, dedicated servers, unit count, open world. Deterministic lockstep for high unit counts / low player counts; Quake model for hyper-competitive FPS; GGPO for fighters. Maps ngame toward **server-driven deterministic** + light predict. |

### Prediction, rollback, recovery

| Source | Summary |
|--------|---------|
| [GGPO](https://www.ggpo.net/) / [DeveloperGuide](https://github.com/pond3r/ggpo/blob/master/doc/DeveloperGuide.md) | Speculative local advance + last-input remote guess; on mismatch Save→load→resim without draw. Requires deterministic sim + save/load/step. SyncTest: forced 1-frame rollback every frame to find leaks. |
| [Game Developer — GGPO lag-fighting](https://www.gamedeveloper.com/programming/the-lag-fighting-techniques-behind-ggpo-s-netcode) | Hide RTT in **remote** startup, keep local avatar offline-snappy; rollback corrects wrong guesses. |
| [SnapNet — Part 2: Rollback](https://www.snapnet.dev/blog/netcode-architectures-part-2-rollback/) | Rollback as lockstep extension. Mix **input delay** (consistency) + **predict** (feel); example: ≤50 ms delay, next ~100 ms predict, then more delay. |
| [SnapNet — Input Delay vs Rollback](https://www.snapnet.dev/docs/core-concepts/input-delay-vs-rollback/) | Delay = wait for authority (no pops, more lag). Predict = immediate local (pops + CPU on reconcile). Tune both. |
| [Klotho SynchronizationDesign](https://github.com/xpTURN/Klotho/blob/main/Docs/SynchronizationDesign.md) | Verified vs Predicted chains; server-driven verified timeline; soft/hard predict throttle; recovery ladder hash → rollback → full-state → corrective reset. Closest conceptual match to ngame’s confirm + Save + soft PHYS. |

### Fast-paced auth server (non-lockstep contrast)

| Source | Summary |
|--------|---------|
| [Gabriel Gambetta — Fast-Paced Multiplayer](https://www.gabrielgambetta.com/client-server-game-architecture.html) | Auth server + client prediction + reconciliation + entity interpolation. Teaches ping math and why dumb clients feel awful. Contrast: usually **state** reconciliation, not bit-identical shared physics. |
| [Valve — Source Multiplayer Networking](https://developer.valvesoftware.com/wiki/Source_Multiplayer_Networking) | Snapshots, tick, interp, client prediction, lag compensation for hitscan. Bandwidth/throttle via updaterate/cmdrate. Different genre toolkit; useful for “feel under ping” vocabulary. |
| [Bernier — Latency Compensating Methods](https://www.gamedevs.org/uploads/latency-compensation-in-client-server-protocols.pdf) | Classic GDC/paper: server rewind for hit registration. Shows FPS-style fairness vs deterministic shared world (ngame doesn’t rewind Box3D for hits — confirm owns truth). |

### Extra pointers

| Source | Summary |
|--------|---------|
| [Gaffer — Snapshot interpolation / compression / state sync](https://gafferongames.com/post/snapshot_interpolation/) | When determinism fails or player count grows: stream state, interp, compress, prioritize. ngame non-lockstep scenes. |
| [Factorio wiki — Desynchronization](https://wiki.factorio.com/Desynchronization) | Desync dumps / checksum culture for long sessions. |
| [Vaclav Samec — Deterministic lockstep](https://vaclavsamec.com/blog_lockstep/) | Short practical restatement of frame-tagged inputs + UDP redundancy + sync window. |
| [GameNetworkingResources](https://github.com/petitgamedev/GameNetworkingResources) | Curated index (Gaffer, Valve, GGPO, Overwatch GDC, etc.). |

---

## ngame lockstep (vs classic Gaffer #1)

One code path for multi-peer `sim: "lockstep"`. Compared to Gaffer article #1’s pure `all_have` hitch model:

| | Classic Gaffer #1 (literature) | ngame lockstep (what ships) |
|--|-------------------------------|-----------------------------|
| Step when | `all_have(next)` | Host `LOCK_CONFIRM` for next |
| Missing input | STALL (hitch together) | After `NG_LOCK_CONFIRM_SEC`, **zeros** in confirm (same vector for all) |
| Late input | Conflict / warn | **Drop** if `tick ≤ confirmed` (still heartbeat) |
| Prediction | None | Last-input hold ≤ `NG_LOCK_PREDICT_MAX` (8); Save ring for rollback |
| Far behind / hash diverge | Hitch / stuck | Soft `LOCK_PHYS` (that peer only) + confirm continues; silent prune after `NG_LOCK_SILENT_SEC` |
| Confirm wire | n/a | **Reliable** + unsent hist flush (contiguous apply) |
| Literature | Deterministic Lockstep | GGPO-style predict + Factorio-style host merge/confirm + Klotho-style hash→state heal |

**Local rollback vs net PHYS:** mispredict → Restore local Save + resim (nothing on wire). Failed restore / persistent `LOCK_HASH` mismatch → host soft PHYS to **that peer only**. Do **not** use `b3Recording`/`b3RecPlayer` for net rollback. Do **not** fanout PHYS to healthy peers.

Gate **STALL** still applies for `syncing` / `await_phys` / empty roster BUFFER — **not** for hash mismatch (heal via PHYS).

### Box3D substrate (why lockstep physics is viable)

Box3D is not “almost deterministic.” Per Erin Catto / in-tree docs:

| Level | What Box3D guarantees |
|-------|----------------------|
| Algorithmic | No engine RNG; same inputs → same path |
| Multithreaded | Worker count can differ; order from creation / bit merges, not race timing |
| Cross-platform | 64-bit targets with correct flags: no `-ffast-math`, `-ffp-contract=off`, custom sin/cos/atan2; CI Falling Hinges across compilers/ISAs |

**Save snippets (ngame):** `b3World_Save` / `b3World_Restore` dump the **full seed snapshot** (contacts, warm-start impulses, islands, broad-phase, geometry registry) — the same substrate recordings use. That is the right blob for:

- late-join / soft `LOCK_PHYS`
- local predict Save ring (`NG_PHYS_SAVE_RING`) before confirm rollback

**Not for net:** `b3Recording` / `b3RecPlayer` (debug, scrub, diverge frame). Pose/vel-only teleports are **not** rollback — FAQ/blog: engines cache internal state; partial restore → perpetual desync.

**Still your job:** fixed Δt + identical confirmed input vector; no host-only forces; deterministic body create order; no custom friction/preSolve that differs per peer; don’t ship mismatched SIMD/layout builds (Save layout hash rejects). Double-precision large-world mode must match across peers if enabled.

### Recovery ladder

```text
confirm input mismatch → Restore save@(confirm-1) + resim
                      → missing save / fail → await_phys (mirror)
LOCK_HASH ≠ host      → streak (NG_LOCK_DESYNC_PHYS_STREAK=2)
                      → soft LOCK_PHYS @ confirmed + world_hash
                      → import + re-hash verify → soft snap clock
                      → retry ≤2 → ERROR (no freeze-all)
lag ≥ CATCHUP_TICKS   → same soft PHYS path
late join             → PAUSE → PHYS → READY → RESUME
```

---

## Three strategies (one-line each)

| Strategy | Wire | Receive side | Needs bit-identical sim? |
|----------|------|--------------|--------------------------|
| **Deterministic lockstep** | Inputs only (frame-tagged) | Full physics sim | **Yes** |
| **Snapshot interpolation** | Full visual state snapshots | No sim — buffer + interpolate | No |
| **State synchronization** | Inputs + prioritized state (incl. velocities) | Sim both sides; apply sparse updates | No (approximate) |

---

## 1) Deterministic lockstep — essentials (Gaffer #1)

**Idea:** same initial state + same inputs + same fixed Δt → **exactly** the same result (bit-level; checksum-identical). Bandwidth ∝ input size, **not** object count — the lever for **mass** entities under an authoritative confirm host.

### Rules that matter

1. Sample a **per-frame input struct** (not press/release events). Apply input *n* only on frame *n*.
2. **Cannot step frame *n* without input *n*** — classic #1 waits (hitch); ngame confirms zeros after the deadline.
3. **Playout delay buffer** — intentionally delay dequeue so jittered packets arrive as a steady 1/60 s stream.
4. Prefer **UDP + redundant unacked inputs** over TCP for the input path.
5. Cap catch-up frames per render frame (**4**) to avoid spiral-of-death.
6. Classic #1 recommends **~2–4 players** waiting on the slowest; ngame host confirm + zero-fill + prune targets **mass peers** without freezing healthy clients.

### Smooth vs lag

- **Smooth physics:** yes — real sim both sides, fixed dt, when the playout buffer stays fed.
- **Without lagging:** no in classic #1 — smoothness is bought with playout (+ RTT). ngame adds limited last-input predict; still not full GGPO fighter feel.
- UDP redundancy removes *retransmit* stalls; it does **not** remove “wait if input *n* isn’t here” in the pure hitch model.

### Determinism pitfalls

- **Engine:** Box3D covers XP + MT float determinism when build flags match (see Box3D substrate). Breaking `-ffp-contract=off` / enabling fast-math / mismatched SIMD width breaks the contract.
- **App:** “Almost the same” game logic still diverges forever — script order, unsynced RNG, peer-local forces, different body create order.
- **Partial rollback:** restoring only transforms/velocities without Save snapshot ≠ bit-identical continue (Erin: no pose-only roll-back determinism).
- **Heal path:** `LOCK_HASH` + soft PHYS still required for join, loss, and app bugs — not because Box3D “isn’t deterministic.”

### TCP vs UDP (his demos)

- TCP + loss: sim freezes while waiting for frame *n* retransmit.
- UDP + redundant window: next packet carries missing inputs (if the window covers them).

ngame: `LOCK_INPUT` unreliable/redundant; `LOCK_CONFIRM` / SESSION / PHYS / RESUME reliable.

---

## 2) Snapshot interpolation — essentials

**When:** sim not deterministic, or more players than lockstep can tolerate **without** an input-merge authority.

**Idea:** authority runs physics; viewers buffer snapshots and interpolate (no bit-identical sim).

Valuable notes: snapshots are time-critical not reliable; interp delay ≈ multi× send interval; hermite (pos+vel) beats linear lerp; extrapolation of contacts looks wrong.

**Trade:** high bandwidth for no determinism / no wait-for-all-inputs. ngame non-lockstep pose stream is in this family.

---

## 3) Snapshot compression — role

Make higher snapshot rates affordable so interpolation delay can drop.

https://gafferongames.com/post/snapshot_compression/

---

## 4) State synchronization — essentials

**Idea:** run sim **both** sides **and** send inputs + **sparse** state (pos, orient, velocities). Priority accumulator picks who fits the bandwidth budget. Approximate — expect pops.

---

## Smooth physics without lagging? (verdict)

| Approach | Smooth motion | Input / view latency | Stall on loss? |
|----------|---------------|----------------------|----------------|
| Classic lockstep + playout | Best (real physics) | **Yes** — by design | **Yes** if input *n* missing |
| ngame confirm + light predict | Best when confirms flow | Lower hitch; predict ≤8 | Soft PHYS / zero-fill instead of forever stall |
| Snapshots + interp | Visual approx | Interp buffer | No (skip snapshot) |
| State sync | Continuous between updates | Lower stall; pops | No |
| Full GGPO | Best feel locally | Mispredict pops; CPU | No wait; rollback cost |

Gaffer: **smooth ≠ lagless**. Lagless feel needs deep prediction/rollback (GGPO) or a non-lockstep strategy. Mass + deterministic physics under one host favors **confirm + shallow predict + heal**, not pure peer hitch or pure FPS state stream.

---

## Pitfall checklist (lockstep-first)

- [x] Fixed Δt, same step order, same initial state
- [x] Frame-tagged input samples (not events)
- [x] Playout sized for real jitter — **6 ticks / ~100 ms**
- [x] UDP redundant unacked inputs + acks (`LOCK_INPUT`)
- [x] Catch-up frame cap (`NG_MOD_FIXED_MAX_STEPS` = 4)
- [x] Checksums detect desync **and** host soft PHYS heals (`NG_LOCK_DESYNC_PHYS_STREAK`)
- [x] Late-join state dump (PAUSE/PHYS/READY/RESUME)
- [x] Zeros only inside host CONFIRM (all peers agree)
- [x] Box3D XP/MT determinism (engine) + app input/order discipline
- [x] World Save snippets for PHYS + predict ring (`b3World_Save` / Restore)
- [ ] Player count stress / extreme loss — host confirm mitigates classic “wait for slowest,” not magic
- [ ] Adaptive playout / per-peer latency (Factorio / SnapNet) — further
- [ ] SyncTest-style forced rollback (GGPO) / `b3ValidateReplay` in CI — further
- [ ] Cross-peer layout/SIMD matrix in CI (Save hash gate already rejects mismatch)

---

## Map to ngame

| Concept | ngame |
|---------|-------|
| `sim: "lockstep"` | [`docs/scenes.md`](scenes.md) |
| Playout | `NG_LOCK_PLAYOUT_TICKS` (6), `mod_lockstep_set_playout_ticks` |
| Gate | `mod_lockstep_gate` → GO / BUFFER / STALL |
| Input / ack / hash | `LOCK_INPUT`, `LOCK_ACK`, `LOCK_HASH` |
| Confirm | `LOCK_CONFIRM`, `NG_LOCK_CONFIRM_SEC`, hist/`confirm_bcast`, reliable broadcast |
| Predict / Save | `NG_LOCK_PREDICT_MAX`, `NG_PHYS_SAVE_RING` (≥ predict+2), `mod_scene_physics_save_ring_*` via `b3World_Save`/`Restore` |
| Box3D | `third_party/box3d/` — XP determinism; Save blob = recording seed snapshot; recording/replay debug-only |
| Late join | `LOCK_PAUSE` / `LOCK_PHYS` / `LOCK_READY` / `LOCK_RESUME` |
| Lag / hash heal | `NG_LOCK_CATCHUP_TICKS`, `NG_LOCK_DESYNC_PHYS_STREAK`, soft PHYS + `world_hash` verify, `mod_lockstep_on_soft_phys` |
| Silent peer | `NG_LOCK_SILENT_SEC` prune + roster RESUME |
| Mirror net lost | `mod_lockstep_on_net_lost` |
| Catch-up cap | `NG_MOD_FIXED_MAX_STEPS` |

Implementation: `src/scene/lockstep.c`, `src/net/mod_net.c`, proto `NG_PROTO_VERSION` 9 (`world_hash` on PHYS).

---

## ngame alignment decisions (anti-drift)

Do **not** “optimize latency” by undoing these:

1. **Playout ≥ ~6 ticks @ 60 Hz** — Gaffer trades lag for smoothness.
2. **Mirrors never invent inputs unilaterally** — step on host `LOCK_CONFIRM`. Zero-fill only inside CONFIRM.
3. **`LOCK_INPUT` stays unreliable/redundant** — one ENet socket; ch0 unreliable inputs / ch1 reliable SESSION/PHYS/RESUME/**CONFIRM**. Do not put the input *stream* on reliable-only.
4. **One cold roster** — no flush-time / periodic roster pulses (starve UDP inputs).
5. **Join abort must `end_sync`** — failed READY must not leave `syncing` forever.
6. **Catch-up ≤ 4 fixed steps / frame**.
7. **Smooth ≠ lagless** — do not break determinism for snappier keys.
8. **Do not soft-cap owner on acks alone** — deadlock when acks late.
9. **Owner does not invent zeros outside CONFIRM**.
10. **Owner `peer_count==0` → BUFFER**.
11. **Scene load sends SESSION roster once**.
12. **Late-join PHYS is joiner-only** among *healthy* peers. Soft PHYS may also target a **lagging or hash-desynced** peer only — never fanout re-import to everyone.
13. **Sample inputs while paused** (`gen_local` during syncing/await).
14. **Tip ≤ sim + playout**.
15. **Reduce, don’t band-aid** — fix missing input *n* / divergent clocks.
16. **Send window = newest ≤ INPUT_MAX**.
17. **No tip growth during PAUSE / resume_barrier**.
18. **Joiner keeps `await_phys` until RESUME**.
19. **PHYS never fanout to peers already on the host timeline** — join / lag / hash-resync targets only.
20. **Disconnect drops peer from roster** + reliable RESUME.
21. **Same-scene reload** via `scene_gen` in SESSION when `!syncing`.
22. **ENet timeout ≥ ~5s**.
23. **Empty lobby clears join stall**.
24. **No lockstep entity SNAP fanout on scene load**.
25. **Client input is wall-clock paced**.
26. **Silent peer:** soft zero-fill → catchup PHYS → hard prune.
27. **Mirror upstream loss clears roster**.
28. **Local Save ring for mispredict**; net PHYS for join / lag / hash heal — not every mispredict.
29. **CONFIRM: reliable + unsent hist** so multi-step frames cannot skip ticks on the wire. Keep inputs off the reliable storm path.
30. **Goals stay aligned:** authoritative confirm host + deterministic Box3D + isolate bad peers — do not regress to pure all-have hitch or to FPS state-as-truth for lockstep scenes.
31. **Trust Box3D XP determinism** — keep `-ffp-contract=off`, no fast-math, matching SIMD/layout for Save blobs; do not “fix” desync by abandoning lockstep for pose streams.
32. **Save snippets only at step boundaries** on unlocked worlds; never use `b3RecPlayer` as the net heal path.

<!-- agent: composer-2.5 | 2026-07-31 | gaffer article ladder cleanup | 5bac62 -->
<!-- agent: composer-2.5 | 2026-07-31 | drop mode a/b labels | e8baf8 -->
<!-- agent: composer-2.5 | 2026-07-31 | curated net sources goals | c18378 -->
<!-- agent: composer-2.5 | 2026-07-31 | ngame goals tradeoffs frame | 853c5b -->
<!-- agent: composer-2.5 | 2026-07-31 | box3d determinism save notes | 56d64a -->
