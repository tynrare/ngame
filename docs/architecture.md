# Architecture

## You edit

| Path | Role |
|------|------|
| `res/boot.js` | **Server startup** — registers scenes, `global.module(Boot)` |
| `res/helpers.js` | **Optional shortcuts** — used by `example` and test fixtures |
| `res/scenes/*.js` | **Scene modules** — `global.module(Ctor)`; load with `scene <id>` |
| `res/modules/*.js` | **Feature modules** — wired into scenes via `register` + `wire` |
| `res/shaders/*.fs` | Shader sources for describes |

```
res/
  boot.js              ← server starts here; register("sphere", "scenes/sphere.js")
  helpers.js
  modules/
    sample_shooting.js ← wire from stacking (F to fire)
  scenes/
    stacking.js
    cube.js
    …
  shaders/
tests/scenes/          ← CI fixtures (owner, local)
```

### JS modules (`register` / `module` / `wire`)

- `global.module(Ctor)` — file export (constructor). Required once per file.
- `global.register(id, path)` — C-persistent catalog (survives Duktape heap teardown on scene change).
  - Res-root: `"scenes/foo.js"` → `res/scenes/foo.js`
  - Relative: `"./…"` / `"../…"` against the file currently being evaluated
- `global.wire(id)` — load + `new` + attach; lifecycle after the scene (`init` runs immediately from `wire`).
- `global.action_register(this, name)` / `global.action(name, …)` — lockstep POD actions (see [scenes.md](scenes.md)); `get_local_input` for propose edges.
- `get_any_input` / `get_local_input` / `get_peer_input(key, id)` — input sampling (`get_input` remains an alias of `get_any_input`).

<!-- agent: composer-2.5 | 2026-08-01 | docs js module wiring | 708913 -->
<!-- agent: composer-2.5 | 2026-08-01 | docs lockstep js actions | 7f3a91 -->
<!-- agent: composer-2.5 | 2026-08-01 | rename input get_local_any_peer | 5df554 -->
## Engine

Every `ngame` client runs a **gateway** (local server + thin render view):

```
ngame process
  local server (sim, scene, script, MCP) ←loopback→ render + input + console
       ↑ optional upstream ENet
  dedicated host (ngame_server or ngame --server)
```

- **`--local`** (native default): spawns `ngame_server`, gateway upstream to `:27015`, dependent MCP port assigned by root via `REGISTER`
- **`--remote HOST:PORT`**: upstream only; root assigns dependent MCP port
- **`--solo`** (web default, alias `--embedded`): loopback only, authoritative local server, MCP on `27100`
- **`--server`**: dedicated headless host (same module as `ngame_server`); ENet HOST + WS + root MCP `27100`; no window
- **`--server --connect HOST:PORT --port LISTEN`**: **proxy** — listen for children + upstream CLIENT to parent (nested lockstep scopes)

### Net facets (compose on one `mod_net` path)

| Facet | Meaning |
|-------|---------|
| `listen` | ENet HOST (+ WS when headless) for direct children |
| `upstream` | ENet CLIENT to parent |
| `view` | optional local render / loopback seat |
| `clock_owner` | confirm for **local** listen roster only |

| Entry | Facets |
|-------|--------|
| `--server` / `ngame_server` | `listen` (+ `upstream` if `--connect`) |
| `--solo` | `listen` (loopback) + `view` |
| `--remote` / `--local` gateway | `listen` (loopback) + `upstream` + `view` |
| Proxy B | `listen` + `upstream` (headless; optional `view` later) |

**Nested scopes:** each process with children is clock owner for **direct** peers only. Children collapse to **one** parent peer (`bits` OR; at most one action per uplink tip). Parent roster does not include grandchildren. Prove each scope’s lockstep independently; uplink merge is a separate seat on the parent.

`ngame_server` is the **pack-only** binary (no raylib). Behavior matches `./ngame --server` (proxy uplink on pack binary is not wired — use `./ngame --server --connect`).

Root keeps MCP on `27100`. Dependents send `NG_PKT_REGISTER` on upstream connect; root replies with `REGISTER_ACK` and an agent port from pool (`27101+`). Nested proxies probe the next free MCP port if `27100` is busy.

<!-- agent: composer-2.5 | 2026-08-09 | docs nested proxy facets | 3eab21 -->

Server and view use separate scene runtimes (`g_scene_server` / `g_scene_view`): boot and sim mirror the server runtime; loopback/upstream wire packets apply only to the view runtime and render.

Remote dedicated host uses the same server module stack (`ng_app_server` / `ng_server_runtime`).

`boot.js` / `scenes/*.js` → `src/scene/` → local server → wire → `src/client/render`

<!-- agent: composer-2.5 | 2026-08-09 | docs dual server entry | ec3ea1 -->
<!-- agent: cursor-grok-4.5 | 2026-07-31 | gateway sim wall clock note | 64a891 -->
**Scheduling:** gateway sim/net is **wall-clock paced** (≈60 Hz slices, catch-up when a frame is late). Render is a thin present after the pump — `SetTargetFPS` / swap must not own lockstep input production. An unfocused window must not freeze other peers.

See [scenes.md](scenes.md).
Agent execution checklist: [agent-runbook.md](agent-runbook.md).

<!-- agent: composer-2.5 | 2026-07-30 | docs architecture sim sync layers | 65f300 -->
<!-- agent: cursor-grok-4.5 | 2026-07-31 | gateway sim wall clock note | 64a891 -->
## Physics sim vs entity sync

Two independent channels (see [scenes.md](scenes.md#sim-vs-sync-two-layers)):

- **`scene.sim`** — Box3D for entities with `body`: `lockstep` (all peers + `LOCK_*` packets) or default server-auth (host sim + pose stream).
- **`entity.sync`** — bodiless transform authorship (`shared` / `owner` / …) via `STATE_UPDATE`, cube-compatible even inside a lockstep scene.

Net flush runs both when lockstep is active: lockstep inputs/acks/hashes, plus `STATE_UPDATE` for non-body entities only.

<!-- agent: composer-2.5 | 2026-07-29 | add agent runbook link | d1a8b4 -->
<!-- agent: composer-2.5 | 2026-07-29 | dual runtime register ports | d7e8f9 -->
<!-- agent: composer-2.5 | 2026-07-29 | document physics body fixed_step | 98abb7 -->
<!-- agent: composer-2.5 | 2026-07-30 | docs architecture sim sync layers | 65f300 -->
<!-- agent: cursor-grok-4.5 | 2026-07-31 | gateway sim wall clock note | 64a891 -->
<!-- agent: composer-2.5 | 2026-08-01 | docs js module wiring | 708913 -->
<!-- agent: composer-2.5 | 2026-08-01 | docs lockstep js actions | 47c632 -->
<!-- agent: composer-2.5 | 2026-08-01 | rename input get_local_any_peer | 5df554 -->
<!-- agent: composer-2.5 | 2026-08-09 | docs dual server entry | ec3ea1 -->
<!-- agent: composer-2.5 | 2026-08-09 | docs nested proxy facets | 3eab21 -->
