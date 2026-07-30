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
    physics.js      ← body/shape + box3d fixed-step demo
    example.js      ← demo using helpers.js
  shaders/
tests/scenes/       ← CI fixtures (owner, local)
```

## Engine

Every `ngame` client runs a **gateway** (local server + thin render view):

```
ngame process
  local server (sim, scene, script, MCP) ←loopback→ render + input + console
       ↑ optional upstream ENet
  ngame_server (authoritative multiplayer)
```

- **`--local`** (native default): spawns `ngame_server`, gateway upstream to `:27015`, dependent MCP port assigned by root via `REGISTER`
- **`--remote HOST:PORT`**: upstream only; root assigns dependent MCP port
- **`--solo`** (web default, alias `--embedded`): loopback only, authoritative local server, MCP on `27100`

Root keeps MCP on `27100`. Dependents send `NG_PKT_REGISTER` on upstream connect; root replies with `REGISTER_ACK` and an agent port from pool (`27101+`).

Server and view use separate scene runtimes (`g_scene_server` / `g_scene_view`): boot and sim mirror the server runtime; loopback/upstream wire packets apply only to the view runtime and render.

Remote `ngame_server` uses the same server module stack (`ng_server_runtime`).

`boot.js` / `scenes/*.js` → `src/scene/` → local server → wire → `src/client/render`

See [scenes.md](scenes.md).
Agent execution checklist: [agent-runbook.md](agent-runbook.md).

<!-- agent: composer-2.5 | 2026-07-30 | docs architecture sim sync layers | 65f300 -->
## Physics sim vs entity sync

Two independent channels (see [scenes.md](scenes.md#sim-vs-sync-two-layers)):

- **`scene.sim`** — Box3D for entities with `body`: `lockstep` (all peers + `LOCK_*` packets) or default server-auth (host sim + pose stream).
- **`entity.sync`** — bodiless transform authorship (`shared` / `owner` / …) via `STATE_UPDATE`, cube-compatible even inside a lockstep scene.

Net flush runs both when lockstep is active: lockstep inputs/acks/hashes, plus `STATE_UPDATE` for non-body entities only.

<!-- agent: composer-2.5 | 2026-07-29 | add agent runbook link | d1a8b4 -->
<!-- agent: composer-2.5 | 2026-07-29 | dual runtime register ports | d7e8f9 -->
<!-- agent: composer-2.5 | 2026-07-29 | document physics body fixed_step | 98abb7 -->
<!-- agent: composer-2.5 | 2026-07-30 | docs architecture sim sync layers | 65f300 -->
