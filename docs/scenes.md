# Scenes

Load with `scene <id>`: **`cube`**, **`sphere`**, or **`example`** (helpers demo).

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

`init` → `start(session)` → `step(dt)` → `stop` / `dispose`

## Test fixtures

`tests/scenes/owner.js`, `local.js` — sync smoke tests only.

<!-- agent: composer-2.5 | 2026-07-28 | boot helpers example layout | s6c7n8 -->
<!-- agent: composer-2.5 | 2026-07-29 | multi-instance spawn docs | a75cfb -->
