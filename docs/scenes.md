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

<!-- agent: composer-2.5 | 2026-07-29 | lockstep docs scenes | ea6409 -->
### Lockstep sim

Scene-level mode (one world mode per scene — do not mix with transform-authority bodies):

```javascript
global.describe("scene", "view", { sim: "lockstep", bg: {...}, camera: {...} });
```

In lockstep, every peer creates and steps the same bodies; render uses local transforms (no `STATE_UPDATE` authority). Peers exchange empty tick-indexed `LOCK_INPUT` / `LOCK_ACK` (and optional `LOCK_HASH`) via the server relay, with ~100ms playout delay before the first step.

<!-- agent: composer-2.5 | 2026-07-30 | docs lockstep late join | fd2d78 -->
Mid-sim join pauses all peers (`LOCK_PAUSE`), sends a Box3D world save (`LOCK_PHYS` chunks) to the joiner, verifies checksum (`LOCK_READY`), then resumes (`LOCK_RESUME`) from the shared `sim_tick`. Simulation stalls while the joiner fetches.

<!-- agent: composer-2.5 | 2026-07-30 | lockstep sync docs | 00d907 -->
Entity `sync` / transform bandwidth are **ignored** for entities with a `body` while `sim: "lockstep"` is active: all peers spawn and step those bodies locally; no pose authority over the wire.

Agent: `lockstep_hash` returns the current physics transform checksum and tick.

For non-lockstep scenes, body create/step follows entity sync: `server` on server host, `shared`/`local` on views, `owner` on controller. Do not mix local debris with networked bodies in one contact island.

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

| Mode | Create | Join / materialize | Author |
|------|--------|--------------------|--------|
| `shared` | every view | session match | all views |
| `owner` | controller | all views from session | controller |
| `server` | server | views from session | server |
| `local` | each peer | not in session | local only |

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

<!-- agent: composer-2.5 | 2026-07-30 | docs lockstep late join | fd2d78 -->
