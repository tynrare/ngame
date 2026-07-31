<!-- agent: composer-2.5 | 2026-07-31 | gaffer article ladder cleanup | 5bac62 -->
# Networked physics & lockstep (ngame notes)

Reference for agents and humans. Pair with [`docs/scenes.md`](scenes.md), `src/scene/lockstep.c`, `src/net/mod_net.c`.

---

## Sources → utilized → further

| Bucket | What | Links |
|--------|------|-------|
| **Primary (Gaffer)** | Networked Physics series 0–4; overview essays; new blog | [0 Intro](https://gafferongames.com/post/introduction_to_networked_physics/), [1 Lockstep](https://gafferongames.com/post/deterministic_lockstep/), [2 Snapshots](https://gafferongames.com/post/snapshot_interpolation/), [3 Compression](https://gafferongames.com/post/snapshot_compression/), [4 State sync](https://gafferongames.com/post/state_synchronization/), [What every programmer…](https://gafferongames.com/post/what_every_programmer_needs_to_know_about_game_networking/), [2004 physics](https://gafferongames.com/post/networked_physics_2004/), [mas-bandwidth.com](https://mas-bandwidth.com/) |
| **Utilized (Mode B)** | Fixed Δt + playout + UDP redundant `LOCK_INPUT`; host `LOCK_CONFIRM` (reliable + hist) + zero-fill deadline; last-input predict ≤8; local `b3World_Save` ring rollback; `LOCK_HASH` gate; late-join PHYS; **hash streak → soft PHYS** + post-import hash verify; lag catchup PHYS | GGPO last-input / save-load ideas; SnapNet rollback overview; Klotho recovery ladder rung 2; Factorio “server truth” framing |
| **Further (not in tree yet)** | GGPO SyncTest / Factorio desync dumps; SnapNet input-decay; Klotho rung 3 PAUSE-all corrective reset; full P2P equal-authority; deep predict ≫8; Gaffer #2/#3 as the lockstep path | [GGPO](https://www.ggpo.net/), [DeveloperGuide](https://github.com/pond3r/ggpo/blob/master/doc/DeveloperGuide.md), [SnapNet rollback](https://www.snapnet.dev/blog/netcode-architectures-part-2-rollback/), [Klotho SynchronizationDesign](https://github.com/xpTURN/Klotho/blob/main/Docs/SynchronizationDesign.md), [Factorio desync](https://wiki.factorio.com/Desynchronization), [mas-bandwidth model choice](https://mas-bandwidth.com/choosing-the-right-network-model-for-your-multiplayer-game/) |

Non-lockstep scenes (`cube` / `sphere`) use pose/vel streams closer to Gaffer #2/#4 — not Mode B.

---

## Mode A vs Mode B

| | Mode A (classic Gaffer #1) | Mode B (ngame default multi-peer) |
|--|----------------------------|-----------------------------------|
| Step when | `all_have(next)` | Host `LOCK_CONFIRM` for next |
| Missing input | STALL (hitch together) | After `NG_LOCK_CONFIRM_SEC`, **zeros** in confirm (same vector for all) |
| Late input | Conflict / warn | **Drop** if `tick ≤ confirmed` (still heartbeat) |
| Prediction | None | Last-input hold ≤ `NG_LOCK_PREDICT_MAX` (8); Save ring for rollback |
| Far behind / hash diverge | Hitch / stuck | Soft `LOCK_PHYS` (that peer only) + confirm continues; silent prune after `NG_LOCK_SILENT_SEC` |
| Confirm wire | n/a | **Reliable** + unsent hist flush (contiguous apply) |
| Literature | Deterministic Lockstep | GGPO-style predict + relay-authoritative confirm + Klotho-style hash→state heal |

**Local rollback vs net PHYS:** mispredict → Restore local Save + resim (nothing on wire). Failed restore / persistent `LOCK_HASH` mismatch → host soft PHYS to **that peer only**. Do **not** use `b3Recording`/`b3RecPlayer` for net rollback. Do **not** fanout PHYS to healthy peers.

Mode A STALL remains for `syncing` / `await_phys` / empty roster BUFFER — **not** for hash mismatch (heal via PHYS).

### Recovery ladder (Mode B)

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

**Idea:** same initial state + same inputs + same fixed Δt → **exactly** the same result (bit-level; checksum-identical). Bandwidth ∝ input size, **not** object count.

### Rules that matter

1. Sample a **per-frame input struct** (not press/release events). Apply input *n* only on frame *n*.
2. **Cannot step frame *n* without input *n*** — missing → wait (hitch/stall) in Mode A; Mode B confirms zeros after deadline.
3. **Playout delay buffer** — intentionally delay dequeue so jittered packets arrive as a steady 1/60 s stream.
4. Prefer **UDP + redundant unacked inputs** over TCP for the input path.
5. Cap catch-up frames per render frame (**4**) to avoid spiral-of-death.
6. Recommend **~2–4 players**. Multi-peer: everyone waits on the **most lagged** peer (Mode A); Mode B host confirm decouples silent peers via zero-fill + PHYS.

### Smooth vs lag

- **Smooth physics:** yes — real sim both sides, fixed dt, when the playout buffer stays fed.
- **Without lagging:** no in classic #1 — smoothness is bought with playout (+ RTT). Mode B adds limited last-input predict; still not lagless GGPO fighting-game feel.
- UDP redundancy removes *retransmit* stalls; it does **not** remove “wait if input *n* isn’t here” in Mode A.

### Determinism pitfalls (his)

- “Almost the same” floating point ≠ lockstep. Divergence compounds forever.
- Same machine + same binary may still fail across compilers / OS / ISA / debug vs release.
- No silver bullet for cross-platform float determinism. ngame assumes same build/arch for lockstep peers; `LOCK_HASH` + PHYS heal runtime disagreement.

### TCP vs UDP (his demos)

- TCP + loss: sim freezes while waiting for frame *n* retransmit.
- UDP + redundant window: next packet carries missing inputs (if the window covers them).

ngame: `LOCK_INPUT` unreliable/redundant; `LOCK_CONFIRM` / SESSION / PHYS / RESUME reliable.

---

## 2) Snapshot interpolation — essentials

**When:** sim not deterministic, or more players than lockstep can tolerate.

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
| Lockstep + playout (Mode A) | Best (real physics) | **Yes** — by design | **Yes** if input *n* missing |
| Mode B confirm + light predict | Best when confirms flow | Lower hitch; predict ≤8 | Soft PHYS / zero-fill instead of forever stall |
| Snapshots + interp | Visual approx | Interp buffer | No (skip snapshot) |
| State sync | Continuous between updates | Lower stall; pops | No |

Gaffer: **smooth ≠ lagless**. Lagless feel needs deep prediction/rollback (GGPO) or a non-lockstep strategy.

---

## Pitfall checklist (lockstep-first)

- [x] Fixed Δt, same step order, same initial state
- [x] Frame-tagged input samples (not events)
- [x] Playout sized for real jitter — **6 ticks / ~100 ms**
- [x] UDP redundant unacked inputs + acks (`LOCK_INPUT`)
- [x] Catch-up frame cap (`NG_MOD_FIXED_MAX_STEPS` = 4)
- [x] Checksums detect desync **and** host soft PHYS heals (`NG_LOCK_DESYNC_PHYS_STREAK`)
- [x] Late-join state dump (PAUSE/PHYS/READY/RESUME)
- [ ] Player count ≤ ~4 or expect “wait for slowest” / more zero-fill
- [x] Zeros only inside host CONFIRM (all peers agree)

---

## Map to ngame

| Concept | ngame |
|---------|-------|
| `sim: "lockstep"` | [`docs/scenes.md`](scenes.md) |
| Playout | `NG_LOCK_PLAYOUT_TICKS` (6), `mod_lockstep_set_playout_ticks` |
| Gate | `mod_lockstep_gate` → GO / BUFFER / STALL |
| Input / ack / hash | `LOCK_INPUT`, `LOCK_ACK`, `LOCK_HASH` |
| Confirm | `LOCK_CONFIRM`, `NG_LOCK_CONFIRM_SEC`, hist/`confirm_bcast`, reliable broadcast |
| Predict / Save | `NG_LOCK_PREDICT_MAX`, `NG_PHYS_SAVE_RING` (≥ predict+2), `mod_scene_physics_save_ring_*` |
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

<!-- agent: composer-2.5 | 2026-07-31 | gaffer article ladder cleanup | 5bac62 -->
