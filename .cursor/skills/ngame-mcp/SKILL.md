<!-- agent: composer-2.5 | 2026-07-29 | project mcp validation skill | 9ab2e4 -->
# ngame MCP + Validation Skill

Use this skill when changing gateway, scene runtime, rendering synchronization, MCP status output, or smoke tests.

## Inputs to collect first

1. Launch mode involved (`local`, `remote`, `embedded`)
2. Whether behavior is root/server-side, dependent/view-side, or both
3. Which output is authoritative for verification:
   - console `status`
   - MCP `world_snapshot`
   - `scripts/validate.sh` section output

## Required execution order

1. Build:
   - `cmake -B build -S .`
   - `cmake --build build -j"$(nproc)"`
2. Run focused repro and capture output
3. Validate via MCP snapshot(s)
4. Run `./scripts/validate.sh` unless user explicitly asks to skip

## MCP verification checklist

- Query root MCP (`27100`) with `{"cmd":"world_snapshot"}`
- For gateway local/remote, also query dependent MCP (usually `27101+`)
- Confirm fields relevant to the change:
  - `root scene=...`
  - `local scene=...`
  - `view scene=... loaded=...`
  - `render scene=... visible=... bg=...`
  - `gateway upstream=... ready=...`

## Status output checklist

- Detailed status should be on explicit command (`status`/`mcp`)
- Output should be readable (multiline if needed)
- Include launch mode and gateway mode so local vs remote are unambiguous

## Handoff checklist

- State what you changed
- State commands run
- State pass/fail results
- Explicitly call out skipped checks

<!-- agent: composer-2.5 | 2026-07-29 | project mcp validation skill | 9ab2e4 -->
