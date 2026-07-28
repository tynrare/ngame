# Architecture

## You edit

| Path | Role |
|------|------|
| `res/boot.js` | **Server startup** — runs automatically (not in `scenes/`) |
| `res/helpers.js` | **Optional shortcuts** — used by `example` and test fixtures |
| `res/scenes/*.js` | **Scenes** — load with `scene <id>` |
| `res/shaders/*.fs` | Shader sources for describes |

```
res/
  boot.js           ← server starts here
  helpers.js        ← sugar for example + tests
  scenes/
    cube.js         ← full mesh/shader/model/entity describes
    sphere.js
    example.js      ← demo using helpers.js
  shaders/
tests/scenes/       ← CI fixtures (owner, local)
```

## Engine

`boot.js` / `scenes/*.js` → `src/scene/` → `src/server/sim` + `src/net/` → `src/client/render`

See [scenes.md](scenes.md).

<!-- agent: composer-2.5 | 2026-07-28 | boot helpers separate from scenes | a3r4c5 -->
