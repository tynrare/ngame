# Scenes

Load with `scene <id>`: **`cube`**, **`sphere`**, **`physics`**, or **`example`** (helpers demo).

Server **startup** runs `res/boot.js` automatically (not under `scenes/`).

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
| **Physics (`sim`)** | Entities **with** a `body` | Scene `sim: "lockstep"` or default `server` | Lockstep: inputs/acks/hash (+ late-join phys). Server: host Box3D + quantized pose/vel `STATE_UPDATE` |
| **Entity (`sync`)** | Entities **without** a `body` (and non-lockstep bodies) | Entity `sync: shared\|owner\|server\|local` | Cube-compatible `STATE_UPDATE` (POS/ROT/SCALE) |

```text
entity.has_body  →  follow scene.sim
entity.no_body   →  follow entity.sync   // same rules as cube.js
```

A lockstep physics scene may also host bodiless `shared` / `owner` props; those still author and apply transforms independently of the lockstep channel.

### Lockstep sim (`sim: "lockstep"`)

<!-- agent: composer-2.5 | 2026-07-31 | link Gaffer networking article notes | 357765 -->
Background / Gaffer series notes: [`docs/article_gaffer_networking.md`](article_gaffer_networking.md).

```javascript
global.describe("scene", "view", { sim: "lockstep", bg: {...}, camera: {...} });
```

Every peer process runs the **same** body path: capture/share tick inputs → `fixed_step` (`get_input` → forces/torque) → `b3World_Step` → local poses for draw. Lockstep ≠ `sim: "server"` pose authority; do not stream forces.

**One Box3D world per process:** if both server and view scene slots are loaded (embedded), the **server slot is the phys owner** (attach + body `fixed_step` + step once); the view mirrors poses for draw only (no second world / no double step). A pure client with only the view slot owns physics there — that peer still runs the full input→force→step code.

<!-- agent: composer-2.5 | 2026-07-30 | docs lockstep input consumers | 0ecb5e -->
**Inputs:** each gate samples buttons into the slot for `sim_tick+1` (the tick about to step). Peers exchange `LOCK_INPUT` / `LOCK_ACK` / `LOCK_HASH`. Under lockstep, `get_input` reads those slots (solo/`peer_count<=1` may use live keys). Body scripts on the phys owner apply torque/force from that. Conflicting bits for the same peer/tick stall the gate.

<!-- agent: composer-2.5 | 2026-07-30 | docs bandwidth lockstep playout | 563455 -->
**Playout delay:** default `NG_LOCK_PLAYOUT_TICKS` (6 ≈ 100 ms at 60 Hz fixed step) buffers local send-ahead before the first multi-peer sim tick. Runtime: `mod_lockstep_set_playout_ticks` / `mod_lockstep_playout_ticks`.

<!-- agent: composer-2.5 | 2026-07-31 | Mode B lockstep confirm | 8a5fc9 -->
<!-- agent: composer-2.5 | 2026-07-31 | scenes mode b resync note | 4588c4 -->
<!-- agent: composer-2.5 | 2026-07-31 | scenes mode b resync note | 4588c4 -->
**Mode B (default multi-peer):** host broadcasts reliable `LOCK_CONFIRM` (+ hist) for each tick (real inputs or zeros after `NG_LOCK_CONFIRM_SEC`). Peers step on confirmed ticks; mirrors may predict ≤ `NG_LOCK_PREDICT_MAX` with last-input hold and local Save-ring rollback. Late inputs after confirm are dropped (heartbeat kept). Lag ≥ `NG_LOCK_CATCHUP_TICKS` or repeated `LOCK_HASH` mismatch → soft `LOCK_PHYS` to **that peer only** (hash-verified). Classic STALL remains for join/`await_phys`/empty roster — not permanent hash freeze. See `docs/article_gaffer_networking.md` recovery ladder.

**Playout / Mode A note:** Missing peer input before confirm still waits up to the confirm deadline (short hitch), then zero-fills.

<!-- agent: composer-2.5 | 2026-07-30 | docs lockstep late join | fd2d78 -->
<!-- agent: composer-2.5 | 2026-07-30 | docs lockstep one world inputs | b7a451 -->
Mid-sim join pauses all peers (`LOCK_PAUSE`), sends a Box3D world save (`LOCK_PHYS` chunks) to the joiner, verifies checksum (`LOCK_READY`), then resumes (`LOCK_RESUME`) from the shared `sim_tick`. Join hash mismatch **aborts** the join (no RESUME). Periodic `LOCK_HASH` mismatch triggers host soft PHYS to the disagreeing peer (not a permanent STALL). Disconnect removes the peer from the lockstep set.

Entity `sync` on bodies is **ignored** under lockstep (spawn/step/author). Net flush still sends `STATE_UPDATE` for bodiless entities.

Agent: `lockstep_hash` returns the current physics transform checksum and tick.

### Server sim (default)

<!-- agent: composer-2.5 | 2026-07-30 | docs server pose vel stream | 0ab3f0 -->
<!-- agent: composer-2.5 | 2026-07-30 | docs interp kinematic proxies | 610025 -->
Omit `sim`, or use non-`lockstep` (implicit `server`): host runs Box3D for bodies according to entity sync (`server` on host, `shared`/`local` on views, `owner` on controller).

Live replication uses `STATE_UPDATE` with quantized **pose + linear/angular velocity** (cm/s and mrad/s). At-rest bodies stop streaming. After a peer `STATE_ACK`, further updates for that entity are **delta-encoded** (`NG_COMP_FLAGS`) vs the acked baseline. Flush **prioritizes** high |velocity| entities (up to 16 per pass).

Views keep a short sample ring and **Hermite-interpolate** draw pose ~100 ms behind wall clock (extrapolate past the newest sample). For `sync: "server"` bodies, the view also attaches a **kinematic Box3D proxy** driven by the stream (for queries/contacts) — it does not run dynamic simulation.

Do not mix local debris with networked bodies in one contact island.

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

Under `sim: "lockstep"`, bodies ignore this table (all peers create/step locally).

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
<!-- agent: composer-2.5 | 2026-07-31 | Mode B lockstep confirm | 8a5fc9 -->
<!-- agent: composer-2.5 | 2026-07-31 | scenes mode b resync note | 4588c4 -->

