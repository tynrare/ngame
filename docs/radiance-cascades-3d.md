<!-- agent: composer-2.5 | 2026-08-10 | SH bake soft-nearest resolves | 138457 -->
# Radiance Cascades (3D) — North Star

Goal: **dynamic, deterministic GI** (WebGL2/GLES3). Quality = **cost only**.
Product GI = **world-space true Radiance Cascades**. Compose: `Direct + gi * WS * albedo + glow`.
**Glow on surfaces in compose**. **Bloom halo** = later. **SS = shelved**.

## RC essentials

1. Cascade texel = **(probe, dir)** → `(RGB, opacity)`.
2. Interval fill; merge \(I_n + (1-a_n)\,I_f\).
3. **WS** dir-packed volume → **L1 SH per probe** → soft-nearest × **N** on screen.
4. **XZ atlas = debug only**. **SS off charts**.

## Phases

| Phase | Goal | State |
|------:|------|--------|
| 0–4 | gbuf + WS vox + dir-packed RC shell | **done** |
| **5a/b** | True WS RC fill/merge | **done** |
| **5c** | L1 SH bake + soft-nearest resolve | **done** (perf + anti-star) |
| 6 | HRC / sparse | later |
| — | Bloom | later |

## Pipeline (current)

```
gbuffer → vox
→ dir-packed cascade fill → T-merge
→ L1 SH encode (atlas N·4 × N²)
→ soft-nearest SH × N → screen irr
→ compose
```

**Why SH:** per-pixel dir loops (×8 trilinear) = stars + bad cost. SH moves angular integrate to probe grid once; screen is cheap soft-nearest (flat cells, soft faces — no lattice hotspots).

**Quality** scales **dirs / cascades / probe_n**.

## References

- https://radiance.wiki/ · https://jason.today/rc · arXiv:2408.14425

<!-- agent: composer-2.5 | 2026-08-10 | SH bake soft-nearest resolves | 138457 -->
