<!-- agent: grok-4.6 | 2026-08-21 | smoke not product path | 4f0d2e -->
# RC-WS probe smoke (legacy)

Headless CPU cover/split/release/relax oracle for the **old sparse surface
pool**. **Not** the product path ([north star](../../docs/radiance-cascades-3d.md):
chunked static grid + hierarchical interpolated RC).

Kept only while leftover GPU residency code remains.

## Build / run

```bash
cmake --build build -j$(nproc) --target ng_rc_ws_probe_smoke
./build/ng_rc_ws_probe_smoke
```

Exit 0 = PASS (legacy oracle).

<!-- agent: grok-4.6 | 2026-08-21 | smoke not product path | 4f0d2e -->
