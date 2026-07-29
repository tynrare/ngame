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

<!-- agent: composer-2.5 | 2026-07-29 | add agent runbook link | d1a8b4 -->
<!-- agent: composer-2.5 | 2026-07-29 | dual runtime register ports | d7e8f9 -->
