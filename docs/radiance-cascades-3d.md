<!-- agent: composer-2.5 | 2026-08-10 | Phase4 WIP not done | ff9355 -->
# Radiance Cascades (3D) — North Star

Goal: **dynamic, deterministic GI** (WebGL2/GLES3). Quality = **cost only**.
Compose: `Direct + gi * (ws·WS_irr + ss·SS_irr) * albedo + glow`.

## RC essentials

1. One cascade texel = one **(probe, dir)** → `(RGB, opacity)` (SS packed; WS dirs averaged in fill this phase).
2. Interval fill; merge \(I_n + (1-a_n)\,I_f\) (not blur).
3. Direction-first packing (SS). WS = slice-atlas probe volume.
4. **WS = 3D volume** (Phase 4). **XZ atlas = debug only** — not product GI.
5. SS = polish / contact (Phase 3).
6. Butter merged irradiance only (WS).

## Phases

| Phase | Goal | State |
|------:|------|--------|
| 0–1 | Scene + gbuffer | **done** |
| 2 | SS averaged fill/merge shell | done (superseded by 3) |
| 2b | XZ atlas experiments | **debug only** — not product WS |
| **3** | **SS dir-packed RC + T-merge + resolve** | **done** |
| **4** | **WS 3D volume RC + butter** | **WIP** — depth encode + FragCoord atlas |
| 5 | Full bilinear-fix + amortize; SS miss←WS | later |
| 6 | HRC / sparse if needed | later |

**Anti-pattern:** XZ billboard GI (midair discs).

**Done when:** red bleed on surfaces near red walls in final + `debug.render.pass irradiance` (no camera-locked discs).

## Pipeline (Phase 4)

```
gbuffer (depth = cam_dist / FAR in RGBA8)
→ WS: AABB voxelize → vox slice atlas → GPU probe fill (gl_FragCoord) → butter irr volume
→ SS: dir-packed fill → T-merge → resolve → irr_ss
→ compose: Direct + gi * (ws·WS + ss·SS) * albedo + glow
```

Depth: encode `clamp(d/80,0,1)` in gbuf; readers multiply by `FAR=80`.

## References

- https://radiance.wiki/ · direction-first · bilinear-fix
- https://jason.today/rc · https://mini.gmshaders.com/p/radiance-cascades
- https://m4xc.dev/articles/fundamental-rc/ · arXiv:2408.14425 · arXiv:2505.02041

<!-- agent: composer-2.5 | 2026-08-10 | Phase4 WIP not done | ff9355 -->
