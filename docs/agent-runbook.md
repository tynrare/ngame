<!-- agent: composer-2.5 | 2026-07-29 | agent mcp test runbook | e14c6a -->
# Agent Runbook

This runbook is for a clean agent session with no prior chat context.

## 1) Build and baseline checks

From repo root:

```bash
cmake -B build -S .
cmake --build build -j"$(nproc)"
./scripts/validate.sh
```

If `validate.sh` fails, fix the first failing section before moving on.

## 2) Launch mode expectations

- `--local` (native default): starts `ngame_server`, gateway connects upstream to `127.0.0.1:27015`
- `--remote HOST:PORT`: gateway connects to existing upstream server
- `--solo` / `--embedded`: no upstream root; local authoritative loopback runtime

Use console `status` to confirm mode labels:

- `launch=local`
- `launch=remote`
- `launch=embedded`

## 3) MCP quick checks

Root MCP is on `27100`. Dependent/gateway MCP ports are assigned from `27101+`.

Quick probe:

```bash
printf '{"cmd":"world_snapshot"}\n' | nc -q1 127.0.0.1 27100
```

Expected status text includes root/local/view/render/gateway fields, for example:

- `root scene=...`
- `local scene=...`
- `view scene=... loaded=...`
- `render scene=... visible=... bg=...`
- `gateway upstream=... ready=...`

For gateway local mode, probe both:

- `27100` (root/server MCP)
- `27101` (dependent assigned MCP)

## 4) Files and ownership map

- `src/client/` UI/input/render/console-facing logic
- `src/net/` wire protocol integration, upstream/loopback routing, gateway state
- `src/scene/` server/view scene runtimes and JS host bindings
- `src/server/` authoritative runtime (`sim`, `agent`, server app/runtime)
- `res/boot.js` authoritative boot routing entrypoint
- `res/scenes/*.js` loadable scene scripts (`scene <id>`)
- `scripts/validate.sh` canonical smoke workflow

## 5) Required verification after changes

At minimum:

1. Rebuild: `cmake --build build -j"$(nproc)"`
2. Run focused target checks related to changed modules
3. Run `./scripts/validate.sh` before handoff unless a blocker prevents it
4. If validation is partial, report exactly what ran and what did not

## 6) Safe debugging workflow

- Prefer MCP `world_snapshot` over guessing runtime state
- Verify scene changes on both root and view/runtime snapshots
- For gateway issues, compare root (`27100`) vs dependent (`27101+`) MCP outputs
- Keep console output structured and multiline for readability

<!-- agent: composer-2.5 | 2026-07-29 | agent mcp test runbook | e14c6a -->
