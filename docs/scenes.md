# Scenes

<!-- agent: composer-2.5 | 2026-08-01 | list stacking in scenes docs | fcd0a4 -->
Load with `scene <id>`: **`cube`**, **`sphere`**, **`physics`**, **`lockstep`**, **`solar`**, **`stacking`**, or **`example`** (helpers demo).

Server **startup** runs `res/boot.js` automatically (not under `scenes/`). Boot **`register`s** scene ids into a C catalog; scenes export with `global.module(Ctor)`. Feature modules live under `res/modules/` and attach via `register` + `wire` (see [architecture.md](architecture.md)).

<!-- agent: composer-2.5 | 2026-08-01 | docs module register wire | e2e90f -->

## Full describes (cube, sphere, boot)

```javascript
global.describe("mesh", "cube_a_m", { shape: "cube", width: 1, height: 1, depth: 1 });
global.describe("shader", "cube_a_s", { fragment: "shaders/cube.fs", vertex: "shaders/mesh.vs", tint: { r, g, b } });
global.describe("model", "cube_a_mo", { mesh: "cube_a_m", shader: "cube_a_s" });
global.describe("entity", "cube_a_e", { model: "cube_a_mo", func: Cube, sync: "shared" });
global.describe("scene", "view", { bg: { r, g, b }, camera: { ... } });
```

See `res/scenes/cube.js` and `res/scenes/sphere.js`.

## Body / shape (physics)

```javascript
global.describe("shape", "box_shape", { type: "box", hx: 1, hy: 1, hz: 1, density: 1, friction: 0.3 });
global.describe("body", "box_body", { type: "dynamic", shape: "box_shape" }); // static|dynamic|kinematic
global.describe("entity", "box_e", { model: "box_mo", body: "box_body", func: Box, sync: "server" });
```

`body` on entity is optional. Physics steps on a fixed timestep (`fixed_step`); variable `step(dt)` is for input/VFX. See `res/scenes/physics.js`.

Units match box3d HelloWorld: gravity `(0,-10,0)`, ground half-extents `(50,10,50)` at `y=-10` (top at 0), dynamic cube half-extent `1` resting near `y=1`. Use `shaders/flat.fs` for solid meshes (`cube.fs` is a stripe demo shader).

<!-- agent: composer-2.5 | 2026-07-30 | docs dual-channel sim sync | 43d96e -->
## Sim vs sync (two layers)

| Layer | Scope | Flag | Wire |
|-------|--------|------|------|
| **Physics (`sim`)** | Entities **with** a `body` | Scene `sim: "lockstep"` / `"hybrid"` or default `server` | Input-sim: inputs/acks/hash (+ late-join phys). Server: host Box3D + quantized pose/vel `STATE_UPDATE` |
| **Entity (`sync`)** | Entities **without** a `body` (and non-input-sim bodies) | Entity `sync: shared\|owner\|server\|local` | Cube-compatible `STATE_UPDATE` (POS/ROT/SCALE) |

```text
entity.has_body  →  follow scene.sim
entity.no_body   →  follow entity.sync   // same rules as cube.js
```

A lockstep/hybrid physics scene may also host bodiless `shared` / `owner` props; those still author and apply transforms independently of the lockstep channel.

<!-- agent: composer-2.5 | 2026-08-01 | lockstep hybrid sim docs | bd5f9a -->
### Pure lockstep (`sim: "lockstep"`)

Classic Gaffer wait-for-all. Demo: `res/scenes/lockstep.js`. Host confirms only when every peer has the next tick (no deadline zero-fill). Mirrors do not predict. No adaptive/per-peer playout, ghost seats, or soft hash PHYS.

```javascript
global.describe("scene", "view", { sim: "lockstep", bg: {...}, camera: {...} });
```

Every peer process runs the **same** body path: capture/share tick inputs → `fixed_step` (`get_any_input` → forces/torque) → `b3World_Step` → local poses for draw. Missing inputs hitch everyone (gate STALL) until `all_have`. Late join still uses PAUSE → PHYS → RESUME.

### Hybrid sim (`sim: "hybrid"`)

<!-- agent: composer-2.5 | 2026-07-31 | link Gaffer networking article notes | 357765 -->
<!-- agent: composer-2.5 | 2026-07-31 | phase1 lockstep scenes note | 11c570 -->
Phase 1 feel path (`physics.js`, `solar.js`). Goals, knobs, recovery ladder: [`docs/article_gaffer_networking.md`](article_gaffer_networking.md).

```javascript
global.describe("scene", "view", { sim: "hybrid", bg: {...}, camera: {...} });
```

Every peer process runs the **same** body path: capture/share tick inputs → `fixed_step` (`get_any_input` → forces/torque) → `b3World_Step` → local poses for draw. Hybrid ≠ `sim: "server"` pose authority; do not stream forces.

**One Box3D world per process:** if both server and view scene slots are loaded (embedded), the **server slot is the phys owner** (attach + body `fixed_step` + step once); the view mirrors poses for draw only (no second world / no double step). A pure client with only the view slot owns physics there — that peer still runs the full input→force→step code.

<!-- agent: composer-2.5 | 2026-07-30 | docs lockstep input consumers | 0ecb5e -->
**Inputs:** each gate samples buttons into the slot for `sim_tick+1` (the tick about to step). Peers exchange `LOCK_INPUT` / `LOCK_ACK` / `LOCK_HASH`. Under input-sim:
- `get_any_input(key)` — OR of all peers’ committed bits for this step (shared controls; formerly `get_input`)
- `get_local_input(key)` — live view keyboard (propose / local UI)
- `get_peer_input(key, peer_id)` — one peer’s committed bits (`peer_id` 0 → local peer)

Body scripts on the phys owner apply torque/force from that. Conflicting bits for the same peer/tick stall the gate.

<!-- agent: composer-2.5 | 2026-08-01 | docs lockstep js actions | e26fd2 -->
<!-- agent: composer-2.5 | 2026-08-01 | rename input get_local_any_peer | 524507 -->
**Actions (POD):** `global.action_register(this, "action_fire")` then `global.action("action_fire", origin, dir, speed)`. Propose from view `step` with `get_local_input` + camera; args pack as floats (vec3 → 3). Max one action per peer per tick on the wire (`LOCK_INPUT` / `LOCK_CONFIRM`, proto ≥13). Engine dispatches `action_fire(...)` on each heap before `fixed_step` for that tick. Zero-fill / remote predict never hold-fire. See `res/modules/sample_shooting.js`.

<!-- agent: composer-2.5 | 2026-07-30 | docs bandwidth lockstep playout | 563455 -->
**Playout delay:** default `NG_LOCK_PLAYOUT_TICKS` (6 ≈ 100 ms at 60 Hz fixed step) buffers local send-ahead before the first multi-peer sim tick. Runtime: `mod_lockstep_set_playout_ticks` / `mod_lockstep_playout_ticks`.

<!-- agent: composer-2.5 | 2026-07-31 | drop mode b scenes note | 885a6c -->
<!-- agent: composer-2.5 | 2026-07-31 | phase1 lockstep scenes note | 11c570 -->
<!-- agent: composer-2.5 | 2026-08-01 | predict max scenes note | b33b75 -->
**Hybrid (multi-peer):** host broadcasts reliable `LOCK_CONFIRM` (+ hist) for each tick (real inputs or zeros after `NG_LOCK_CONFIRM_SEC`). Peers step on confirmed ticks; mirrors may predict ≤ `NG_LOCK_PREDICT_MAX` (9; effective depth shrinks when playout is high) with last-input hold and local Save-ring rollback. Late inputs after confirm are dropped (heartbeat kept). Lag ≥ `NG_LOCK_CATCHUP_TICKS` or repeated `LOCK_HASH` mismatch → soft `LOCK_PHYS` to **that peer only** (hash-verified). Gate STALL remains for join/`await_phys`/empty roster — not permanent hash freeze. Small-peer bar for now; see article Phase 1 / further steps.

**Playout note:** Missing peer input before confirm still waits up to the confirm deadline (short hitch), then zero-fills.

<!-- agent: composer-2.5 | 2026-07-30 | docs lockstep late join | fd2d78 -->
<!-- agent: composer-2.5 | 2026-07-30 | docs lockstep one world inputs | b7a451 -->
Mid-sim join pauses all peers (`LOCK_PAUSE`), sends a Box3D world save (`LOCK_PHYS` chunks) to the joiner, verifies checksum (`LOCK_READY`), then resumes (`LOCK_RESUME`) from the shared `sim_tick`. Join hash mismatch **aborts** the join (no RESUME). Periodic `LOCK_HASH` mismatch triggers host soft PHYS to the disagreeing peer (not a permanent STALL). Disconnect may ghost-seat (`NG_LOCK_PRUNE_SEC`) for name rebind under hybrid.

Entity `sync` on bodies is **ignored** under lockstep/hybrid (spawn/step/author). Net flush still sends `STATE_UPDATE` for bodiless entities.

Agent: `lockstep_hash` returns the current physics transform checksum and tick.

### Server sim (default)

<!-- agent: composer-2.5 | 2026-07-30 | docs server pose vel stream | 0ab3f0 -->
<!-- agent: composer-2.5 | 2026-07-30 | docs interp kinematic proxies | 610025 -->
<!-- agent: composer-2.5 | 2026-08-01 | scenes server gaffer notes | ad0e58 -->
Omit `sim`, or use non-input-sim (implicit `server`): host runs Box3D for bodies according to entity sync (`server` on host, `shared`/`local` on views, `owner` on controller).

Live replication uses unreliable `STATE_UPDATE` with quantized **pose + lin/ang vel** (cm / mrad) and **smallest-three** orientation (proto v12). At-rest omits vel. For `sync: "server"` only: per-peer ACK **delta** (`NG_COMP_FLAGS`) with absolute keyframes ~every 30 sends; **shared / multi-author stay absolute**. Flush prioritizes |velocity| × interest (R≈40, skip beyond 2R; up to 24 per pass).

Views keep a sample ring and **Hermite**-interpolate with adaptive delay (~3× arrival EMA, clamp 50–350 ms; `NG_STATE_INTERP_MS` override). Past the newest sample, **hold** (no naive extrapolate). For `sync: "server"` bodies, the view attaches a **kinematic Box3D proxy** driven by pose+vel. Controller `owner` bodies: local sim; skip applying absolute host updates when error is below ~5 cm / ~5°.

Do not mix local debris with networked bodies in one contact island. Literature map: [`docs/article_gaffer_networking.md`](article_gaffer_networking.md).


## Spawn (multi-instance)

`describe("entity", ...)` is a prefab. Each `spawn` creates an instance:

```javascript
this.a = global.spawn("cube_a_e", {
  key: "left",                          // optional stable id
  position: { x: -2, y: 0, z: 0 },
  rotation: { x: 0, y: 0, z: 0 },
  scale: 1
});
this.b = global.spawn("cube_a_e", {     // keyless: match by Scene.start call order
  position: { x: 2, y: 0, z: 0 }
});
```

Join matching: optional `key`, else ordinal among `spawn()` calls in `Scene.start` against the session spawn list. Create-time pose lives in spawn opts (not `set_*` in `start`).

## Sync modes

Applies to **bodiless** entities always, and to bodies when `sim` is not lockstep.

| Mode | Create | Join / materialize | Author |
|------|--------|--------------------|--------|
| `shared` | every view | session match | all views |
| `owner` | controller | all views from session | controller |
| `server` | server | views from session | server |
| `local` | each peer | not in session | local only |

Under `sim: "lockstep"` or `sim: "hybrid"`, bodies ignore this table (all peers create/step locally).

## Helpers (optional)

`res/helpers.js` exposes `sceneHelpers.primitive()` for shorter setup. Used by:

- `res/scenes/example.js` — try `scene example`
- `tests/scenes/` fixtures (CI)

Cube and sphere intentionally use full describes.

## Lifecycle

`init` → `start(session)` → `fixed_step(dt)` (0..N/frame) + `step(dt)` → `stop` / `dispose`

Mutate simulation / bodies only in `fixed_step`. Variable `step` is for presentation and input sampling.

## Test fixtures

`tests/scenes/owner.js`, `local.js` — sync smoke tests only.

<!-- agent: composer-2.5 | 2026-07-28 | boot helpers example layout | s6c7n8 -->
<!-- agent: composer-2.5 | 2026-07-29 | multi-instance spawn docs | a75cfb -->
<!-- agent: composer-2.5 | 2026-07-29 | document physics body fixed_step | 19851f -->
<!-- agent: composer-2.5 | 2026-07-29 | lockstep docs scenes | ea6409 -->
<!-- agent: composer-2.5 | 2026-07-30 | lockstep sync docs | 00d907 -->
<!-- agent: composer-2.5 | 2026-07-30 | docs dual-channel sim sync | 43d96e -->
<!-- agent: composer-2.5 | 2026-07-30 | docs server pose vel stream | 0ab3f0 -->
<!-- agent: composer-2.5 | 2026-07-30 | docs interp kinematic proxies | 610025 -->
<!-- agent: composer-2.5 | 2026-07-30 | docs bandwidth lockstep playout | 563455 -->

<!-- agent: composer-2.5 | 2026-07-30 | docs lockstep late join | fd2d78 -->
<!-- agent: composer-2.5 | 2026-07-30 | docs lockstep one world inputs | b7a451 -->
<!-- agent: composer-2.5 | 2026-07-30 | docs lockstep input consumers | 0ecb5e -->
<!-- agent: composer-2.5 | 2026-07-31 | link Gaffer networking article notes | 357765 -->
<!-- agent: composer-2.5 | 2026-07-31 | drop mode b scenes note | 885a6c -->
<!-- agent: composer-2.5 | 2026-07-31 | phase1 lockstep scenes note | 11c570 -->
<!-- agent: composer-2.5 | 2026-08-01 | predict max scenes note | b33b75 -->
<!-- agent: composer-2.5 | 2026-08-01 | lockstep hybrid sim docs | bd5f9a -->
<!-- agent: composer-2.5 | 2026-08-01 | list stacking in scenes docs | fcd0a4 -->
<!-- agent: composer-2.5 | 2026-08-01 | docs module register wire | e2e90f -->
<!-- agent: composer-2.5 | 2026-08-01 | docs lockstep js actions | e26fd2 -->
<!-- agent: composer-2.5 | 2026-08-01 | rename input get_local_any_peer | 524507 -->
