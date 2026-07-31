<!-- agent: composer-2.5 | 2026-07-31 | Gaffer networked physics article notes | e2d8bd -->
# Gaffer — Networked Physics (article notes)

Reference notes from Glenn Fiedler’s **Networked Physics** series. Use alongside ngame lockstep (`docs/scenes.md`, `src/scene/lockstep.c`).

## Series (`NEXT ARTICLE:` chain)

| # | Article | Link |
|---|---------|------|
| 0 | Introduction to Networked Physics | https://gafferongames.com/post/introduction_to_networked_physics/ |
| 1 | Deterministic Lockstep | https://gafferongames.com/post/deterministic_lockstep/ |
| 2 | Snapshot Interpolation | https://gafferongames.com/post/snapshot_interpolation/ |
| 3 | Snapshot Compression | https://gafferongames.com/post/snapshot_compression/ |
| 4 | State Synchronization | https://gafferongames.com/post/state_synchronization/ |

Related (older overview of peer lockstep vs client/server):

- https://gafferongames.com/post/what_every_programmer_needs_to_know_about_game_networking/
- https://gafferongames.com/post/networked_physics_2004/

New blog (he stopped posting on gafferongames): https://mas-bandwidth.com/

---

## Three strategies (one-line each)

| Strategy | Wire | Receive side | Needs bit-identical sim? |
|----------|------|--------------|--------------------------|
| **Deterministic lockstep** | Inputs only (frame-tagged) | Full physics sim | **Yes** |
| **Snapshot interpolation** | Full visual state snapshots | No sim — buffer + interpolate | No |
| **State synchronization** | Inputs + prioritized state (incl. velocities) | Sim both sides; apply sparse updates | No (approximate) |

---

## 1) Deterministic lockstep — essentials

**Idea:** same initial state + same inputs + same fixed Δt → **exactly** the same result (bit-level; checksum-identical). Bandwidth ∝ input size, **not** object count.

### Rules that matter

1. Sample a **per-frame input struct** (not press/release events). Apply input *n* only on frame *n*.
2. **Cannot step frame *n* without input *n*** — missing → wait (hitch/stall).
3. **Playout delay buffer** — intentionally delay dequeue so jittered packets arrive as a steady 1/60 s stream. Buffer too small → hitch; too large → latency players reject.
4. Prefer **UDP + redundant unacked inputs** over TCP. Inputs are tiny; resend everything not yet acked every packet so you never stall waiting for a TCP retransmit.
5. Cap catch-up frames per render frame (he uses **4**) to avoid spiral-of-death.
6. Recommend **~2–4 players**. Multi-peer: everyone waits on the **most lagged** peer.

### Smooth vs lag

- **Smooth physics:** yes — real sim both sides, fixed dt, when the playout buffer stays fed.
- **Without lagging:** no — smoothness is bought with playout delay (+ RTT for remote). There is **no** client prediction / rollback in this article.
- UDP redundancy removes *retransmit* stalls; it does **not** remove “wait if input *n* isn’t here.”

### Snippets (conceptual)

```c
/* Sample once per sim frame — not edge events */
struct Input {
  bool left, right, up, down, space, z;
};

/* Receiver: only step when input for this frame exists */
Input *in = playout_dequeue(frame_n); /* null → wait */
if (!in) return; /* hitch / stall */
simulate(fixed_dt, *in);
```

```text
UDP packet (sender → receiver):
  [ ack_from_peer? ] [ newest_seq ] [ unacked inputs window... ]
  optionally RLE: 1 bit "same as previous" vs 6-bit payload when changed

Receiver → sender each frame:
  [ highest contiguous received input seq ]  // ack; sender drops older
```

```text
Playout (his simple demo):
  first input received → t0 = local_time
  deliver frame n at t0 + 100ms + n * (1/60)
  underrun → null (sim waits)
  burst → dequeue up to 4 frames / render frame
```

### Determinism pitfalls (his)

- “Almost the same” floating point ≠ lockstep. Divergence compounds forever.
- ODE example: internal RNG in constraint order broke sync until `dSetRandomSeed(frame)`.
- Same machine + same binary may still fail across compilers / OS / ISA / debug vs release.
- No silver bullet for cross-platform float determinism.

### TCP vs UDP (his demos)

- TCP + loss: sim freezes while waiting for frame *n* retransmit (~RTT×2+).
- UDP + redundant window: works even under extreme latency/loss in his videos (e.g. 2 s / 25% loss) **because** you don’t wait on retransmit — the next packet already carries the missing inputs (if the window covers them).

---

## 2) Snapshot interpolation — essentials

**When:** sim not deterministic, or more players than lockstep can tolerate.

**Idea:** left runs physics; right **does not**. Send snapshots; buffer; interpolate between delayed samples.

### Valuable notes

- Snapshots are **time-critical but not reliable** — never TCP; skip lost snapshots, don’t stall for resend.
- Raw “render newest snapshot” hitches under jitter → use an **interpolation buffer**.
- Rule of thumb (his): delay ≈ **3× send interval** so two packets in a row can be lost, plus a little jitter. Example @ 10 pps → ~300 ms + jitter ≈ **350 ms**.
- Raise send rate to cut delay: 30 pps → ~150 ms; 60 pps → ~85 ms (needs compression — next article).
- **Linear position lerp** → 1st-order discontinuities; **hermite** (pos + linear vel) looks much better for rigid bodies.
- Orientation: **slerp** was enough; he did not need angular velocity in the snapshot for that demo.
- **Extrapolation** of colliding / constrained rigid bodies looks wrong (floor penetration, spring mispredict, katamari tangents). Prefer more snapshots + shorter buffer over dumb extrapolation.

### Trade

High bandwidth (full visual state × object count) exchanged for no determinism requirement and no wait-for-all-inputs.

---

## 3) Snapshot compression — role

Make higher snapshot rates affordable so interpolation delay can drop. (Delta / quantization / resting objects / etc. — see article.)

https://gafferongames.com/post/snapshot_compression/

---

## 4) State synchronization — essentials

**Idea:** run sim **both** sides **and** send inputs + **sparse** state updates (pos, orient, **linear + angular velocity**).

### Why velocities

Between updates the receive side **extrapolates with the local sim**. Wrong vel → pops when the next update arrives.

### Packet sketch

```c
struct Packet {
  uint32_t sequence;           /* also frame id if both sides @ 60 Hz */
  Input inputs[MaxInputs];     /* redundant window; if missing → keep last input, DON'T stall */
  int num_object_updates;
  StateUpdate state_updates[MaxStateUpdates]; /* e.g. max 64 of 901 cubes */
};
```

### Priority accumulator (valuable)

- Per-object float remembered across frames.
- Each frame: `accum[i] += priority_now(i)` then sort by accum.
- Pack highest until bandwidth budget; **reset accum to 0 only for objects that fit**; leave others so they win next packet.
- Lets you favor player / interacting bodies without starving resting ones forever.
- Bandwidth limit can move with congestion — quality scales automatically.

### Catch

Approximate / lossy. Expect pops and divergence debugging. Easier to ship than perfect lockstep; not bit-identical.

---

## Smooth physics without lagging? (verdict)

| Approach | Smooth motion | Input / view latency | Stall on loss? |
|----------|---------------|----------------------|----------------|
| Lockstep + playout | Best (real physics) | **Yes** — by design | **Yes** if input *n* missing after buffer |
| Snapshots + interp | Visual approx | **Yes** — interp buffer | No (skip snapshot) |
| State sync | Continuous between updates | Lower stall risk; pops instead | No (extrapolate last input) |

Gaffer lockstep: **smooth ≠ lagless**. Lagless responsive feel needs prediction/rollback (not in this series) or a non-lockstep strategy.

---

## Pitfall checklist (lockstep-first)

- [x] Fixed Δt, same step order, same initial state
- [x] Frame-tagged input samples (not events)
- [x] Playout sized for real jitter — **6 ticks / ~100 ms** (measure, don’t guess)
- [x] UDP redundant unacked inputs + acks (not TCP for the critical path)
- [x] Catch-up frame cap (`NG_MOD_FIXED_MAX_STEPS` = 4)
- [x] Checksums / hashes to detect desync early
- [x] Plan late-join (state dump) — pure input stream isn’t enough mid-match
- [ ] Player count ≤ ~4 or expect “wait for slowest”
- [x] Don’t invent different inputs per peer for the same tick (zeros only if **all** peers agree on the fill policy)

---

## Map to ngame (pointers)

| Gaffer concept | ngame |
|----------------|-------|
| Scene `sim: "lockstep"` | `docs/scenes.md` — Lockstep sim |
| Playout delay | `NG_LOCK_PLAYOUT_TICKS` (**6 ≈ 100 ms**), `mod_lockstep_set_playout_ticks` |
| Gate wait / stall | `mod_lockstep_gate` → `GO` / `BUFFER` / `STALL` |
| Input / ack / hash packets | `LOCK_INPUT`, `LOCK_ACK`, `LOCK_HASH` |
| Late join state | `LOCK_PAUSE` / `LOCK_PHYS` / `LOCK_READY` / `LOCK_RESUME` |
| Server pose stream (non-lockstep) | closer to snapshot / state-sync hybrid than pure lockstep |
| Catch-up cap (4) | `NG_MOD_FIXED_MAX_STEPS` in `ng_mod.h` |

Implementation: `src/scene/lockstep.c`, `src/scene/lockstep.h`, flush in `src/net/mod_net.c`.

---

## ngame alignment decisions (anti-drift)

Keep agents honest against this article — do **not** “optimize latency” by undoing these:

1. **Playout ≥ ~6 ticks @ 60 Hz** — Gaffer trades lag for smoothness. Cutting to 2 caused constant STALL underruns.
2. **Mirrors never invent inputs** — `STALL` until `all_have(next_sim)`. **Owner also waits for real inputs** (no zero-fill race — that wraps the ring and freezes mirrors).
3. **UDP-style redundant input window** — do not put critical inputs only on reliable/TCP-ordered paths; roster RESUME may use reliable but must not starve input flush.
4. **Cold-start roster** — send a few RESUME pulses at tick 0, then stop. Do **not** spam reliable roster while waiting for `got_input` (starves UDP inputs → permanent freeze).
5. **Join abort must `end_sync`** — failed READY must not leave `syncing` forever (permanent freeze).
6. **Catch-up ≤ 4 fixed steps / frame** — already `NG_MOD_FIXED_MAX_STEPS`.
7. **Smooth ≠ lagless** — WASD feel comes after playout is healthy; do not re-break determinism for snappier keys.
8. **Do not soft-cap owner on acks alone** — that deadlocks when acks are late; Gaffer waits for *inputs*, then everyone steps together.
9. **Owner does not invent zeros to keep the clock moving** — hitch together (Gaffer) rather than race the ring.
10. **No solo-GO on a transient 1-peer roster** — debounce ~1.5s so peer 2 can appear; early solo-race desyncs permanently.
12. **Re-arm cold roster on sim restart** — scene reload sets `sim_tick=0`; spent roster pulse counters must reset or mirrors BUFFER forever.

<!-- agent: composer-2.5 | 2026-07-31 | Gaffer networked physics article notes | e2d8bd -->
<!-- agent: composer-2.5 | 2026-07-31 | gaffer align notes in article | 4475e8 -->
