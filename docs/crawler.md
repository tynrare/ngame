<!-- agent: grok-4.6 | 2026-08-31 | crawler analog roster docs | 0f33cb -->
<!-- agent: grok-4.6 | 2026-08-31 | hybrid predict interp notes | a8396d -->
# Crawler (scope index)

Square dungeon cell, hybrid lockstep, first-person view. Gameplay in JS; C albedo is engine-only.

**Scope id:** `crawler`  
**Gateway:** [`res/scenes/crawler.js`](../res/scenes/crawler.js) (`crawler-view`, `crawler-pawn`)  
**Related:** [`docs/scenes.md`](scenes.md), [`docs/article_gaffer_networking.md`](article_gaffer_networking.md)

## Patterns

| Flow id | Gateway | Role |
|---------|---------|------|
| `crawler-view` | `res/scenes/crawler.js` `Scene.step` | Mouse look, `set_local_analog(yaw)`, camera at local pawn, Y-billboards |
| `crawler-pawn` | `res/scenes/crawler.js` `Scene.fixed_step` + `Pawn.fixed_step` | Roster from `lockstep_peers()`; WASD rotated by `get_peer_analog` |

## Touchpoints

- Boot catalog: `res/boot.js` `register("crawler", …)`
- Albedo: `describe("model", { albedo })` → flip-on-load + `tex.fs`
- Graph `describe` cap is 32 (mesh/shader/model/shape/body/entity/scene share one table); crawler uses one `cr_tex_s` shader
- Analog: proto v16 lockstep u8 yaw (`τ/256`); camera pitch is local-only
- Feel: hybrid **predict** (host + 1-peer mirrors) + view **Hermite** on remote pawns; local pawn live
- MCP: `screenshot <path.png>` (client), `wire_analog <yaw>`

## Invariants

- One pawn per **connected** peer (`p{id}`); spawn/despawn on confirmed `fixed_step` (late join / disconnect).
- Local mesh scale ~0; remotes, props, and crate **card** Y-billboard. Crate keeps a box collider.
- Walls/floor are static boxes; no capsule mover / hitscan rewind.
- No `look` action; yaw is analog lockstep. Action slot reserved for later fire.

<!-- agent: grok-4.6 | 2026-08-31 | crawler analog roster docs | 0f33cb -->
<!-- agent: grok-4.6 | 2026-08-31 | hybrid predict interp notes | a8396d -->
