<!-- agent: composer-2.5 | 2026-08-10 | glow visible bloom later | c2761f -->
# Radiance Cascades (3D) — North Star

Goal: **dynamic, deterministic GI** (WebGL2/GLES3). Quality = **cost only**.
Compose: `Direct + gi * (ws·WS_irr + ss·SS_irr) * albedo + glow`.
**Glow on surfaces in compose** (emissive visible). **Bloom halo** = later pass.  
**SS does not march glow** (no glow-as-emitter ghosts).

## RC essentials

1. One cascade texel = one **(probe, dir)** → `(RGB, opacity)` (SS packed; WS dirs averaged in fill this phase).
2. Interval fill; merge \(I_n + (1-a_n)\,I_f\) (not blur).
3. Direction-first packing (SS). WS = slice-atlas probe volume.
4. **WS = 3D volume** (Phase 4). **XZ atlas = debug only** — not product GI.
5. **SS = surface color / contact** (optional polish). Default **off** (`ss_weight=0`); skip SS tick when 0. WS = product GI.
6. Butter merged irradiance only (WS).

## Phases

| Phase | Goal | State |
|------:|------|--------|
| 0–1 | Scene + gbuffer | **done** |
| 2 | SS averaged fill/merge shell | done (superseded by 3) |
| 2b | XZ atlas experiments | **debug only** — not product WS |
| **3** | **SS dir-packed RC + T-merge + resolve** | **done** |
| **4** | **WS 3D volume RC + butter** | **done** |
| **5** | Full bilinear-fix + amortize; SS miss←WS | **WIP** — WS trilinear sample done |
| 6 | HRC / sparse if needed | later |
| — | Bloom halo from gbuf glow | later |

**Anti-pattern:** XZ billboard GI; **glow as SS emitters** (4-dir ghosts); 2D-atlas bilinear on Y+Z·N packing (shears).

**Phase 4 done when:** red bleed near red walls in `final` + `debug.render.pass irradiance` (world-locked, no camera discs).

## Pipeline

```
gbuffer (albedo / normal / glow / depth RGB=UVW A=1)
→ WS: AABB voxelize → vox → probe fill → butter
→ SS: (skipped when ss_weight=0) march geo+albedo only → T-merge → resolve
→ compose: Direct + gi * (ws·WS + ss·SS) * albedo + glow
→ bloom halo(glow)   // later — does not replace compose glow
```

WS sample: **manual 8-tap trilinear** in probe-index space (POINT atlas; not HW 2D bilinear).
Butter: shader mix only — no unshadered RT blit.
SS default off until Phase 5 bilinear-fix (avoids 4-dir screen ghosts).
Phase 5 remaining: SS bilinear-fix merge, amortize, miss←WS.

## References

- https://radiance.wiki/ · direction-first · bilinear-fix
- https://jason.today/rc · https://mini.gmshaders.com/p/radiance-cascades
- https://m4xc.dev/articles/fundamental-rc/ · arXiv:2408.14425 · arXiv:2505.02041

<!-- agent: composer-2.5 | 2026-08-10 | glow visible bloom later | c2761f -->
