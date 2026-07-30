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

```javascript
global.describe("scene", "view", { sim: "lockstep", bg: {...}, camera: {...} });
```

Every peer creates and steps the same **bodies**; render uses local body transforms (no pose authority for those entities). Peers exchange tick-indexed `LOCK_INPUT` / `LOCK_ACK` (and optional `LOCK_HASH`) via the server relay, with ~100ms playout delay before the first step.

<!-- agent: composer-2.5 | 2026-07-30 | docs lockstep late join | fd2d78 -->
Mid-sim join pauses all peers (`LOCK_PAUSE`), sends a Box3D world save (`LOCK_PHYS` chunks) to the joiner, verifies checksum (`LOCK_READY`), then resumes (`LOCK_RESUME`) from the shared `sim_tick`. Simulation stalls while the joiner fetches.

Entity `sync` on bodies is **ignored** under lockstep (spawn/step/author). Net flush still sends `STATE_UPDATE` for bodiless entities.

Agent: `lockstep_hash` returns the current physics transform checksum and tick.

### Server sim (default)

<!-- agent: composer-2.5 | 2026-07-30 | docs server pose vel stream | 0ab3f0 -->
Omit `sim`, or use non-`lockstep` (implicit `server`): host runs Box3D for bodies according to entity sync (`server` on host, `shared`/`local` on views, `owner` on controller).

Live replication uses `STATE_UPDATE` with quantized **pose + linear/angular velocity** (cm/s and mrad/s). At-rest bodies stop streaming. Views store the last sample and **extrapolate** draw pose for up to ~120 ms between packets. Clients do not create Box3D bodies for `sync: "server"`.

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

<!-- agent: composer-2.5 | 2026-07-30 | docs lockstep late join | fd2d78 -->
