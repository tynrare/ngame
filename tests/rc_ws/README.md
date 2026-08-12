<!-- agent: grok-4.6 | 2026-08-12 | rc_ws smoke readme | 863117 -->
<!-- agent: grok-4.6 | 2026-08-12 | vis-change smoke readme | 6ab919 -->
<!-- agent: grok-4.6 | 2026-08-12 | persist smoke readme | 176e21 -->
# RC-WS probe smoke (CPU oracle)

Headless CPU cover/split/release/relax oracle for world-space probes. Gates GPU port.

**Policy:** persist keys; cover-first; collapse-relax only when unmet at budget; steal excess only.

## Build / run

```bash
cmake --build build -j$(nproc) --target ng_rc_ws_probe_smoke
./build/ng_rc_ws_probe_smoke
```

Exit 0 = PASS.

## Checks

1. Full `ng_rc_ws_surface_tick`: `used > 8`, `min_lod < 6`
2. Lazy tick: refine + `used ≤ budget`
3. `nk==1` parent refines
4. Cover does not reinsert parent while descendant occupies octant
5. `cull_out` / `cull_in` / `pan_swap` / `steal_over` / `budget_swap` (cull_out uses ±150 vs ±8 groups so coarse cells cannot span both)
6. `full_cover`: fixed vis → `cover_unmet==0`, every prim covered
7. `persist`: after settle, key fingerprint unchanged across more ticks
8. `relax_stuck`: at budget + unmet → collapse frees room; both prims covered
9. `playground`: live ±4 layout; after cover+split `used > 8`, `min_lod < 6`, full cover

## Live MCP (after GPU port / pan)

```bash
printf '{"cmd":"probe_snapshot"}\n' | nc -q1 127.0.0.1 27101
```

After a camera pan, re-query until hist is stable (no glitter) and newly visible areas covered.

<!-- agent: grok-4.6 | 2026-08-12 | rc_ws smoke readme | 863117 -->
<!-- agent: grok-4.6 | 2026-08-12 | vis-change smoke readme | 6ab919 -->
<!-- agent: grok-4.6 | 2026-08-12 | persist smoke readme | 176e21 -->
