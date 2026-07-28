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
