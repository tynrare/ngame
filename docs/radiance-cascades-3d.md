<!-- agent: composer-2.5 | 2026-08-09 | Phase2 RC docs done | a6b410 -->
<!-- agent: composer-2.5 | 2026-08-09 | docs render scene declare | 41a9b6 -->
<!-- agent: composer-2.5 | 2026-08-09 | Phase2 polish docs note | 71cf57 -->
# Radiance Cascades (3D) — North Star

Goal: a **performance-scalable** GI path for ngame, with a clean JS scene
(`res/scenes/rc.js`) and classical material shaders, degrading to simple
direct lighting when cascades are off or cheap.

**Opt-in:** scene `describe("scene","view",{ render: "rc" })` (default `"simple"`).
`rc` includes gbuffer; other scenes stay on simple forward.

## Why

RC (Alexander Sannikov / PoE2) stores a radiance field as **cascades of
radiance intervals**: near = dense probes + few short rays; far = sparse
probes + many long rays (penumbra hypothesis). Merge top→bottom to
reconstruct irradiance without noise.

3D is harder than flatland: full volume cascade 0 ≈ voxelization cost;
screenspace is cheaper but can miss off-screen light unless intervals are
world-space. Community consensus: 2D is solid; **3D is still open** — ship
a ladder, not one fixed algorithm.

## Quality / cost ladder (must ship)

| Level | Name | Cost | Look |
|------:|------|------|------|
| 0 | Direct | O(lights) | N·L + ambient + glow |
| 1 | Direct+glow | cheap | + bloom / high-`glow` surfaces |
| 2 | RC low | 2–3 cascades, nearest merge | soft bounce, coarse |
| 3 | RC mid | + bilinear fix optional | stable merge |
| 4 | RC high | more dirs / probes | target demo look |

**Invariant:** Level 0 always works. Raising level only adds passes.
Knob: `rc_quality` ∈ {0…4} (CLI: `set debug.render.rc_quality`; **2** = SS RC).

## Target deliverables

| Path | Role |
|------|------|
| `docs/radiance-cascades-3d.md` | This north star |
| `res/scenes/rc.js` | Test scene: multi-mesh, lights, glow props |
| `res/shaders/rc.fs` | One material: tint, roughness, metalness, **glow** |
| `res/shaders/rc_gbuf.fs` | G-buffer write (albedo/normal/glow/linear depth) |
| `res/shaders/rc_cascade_fill.fs` | SS interval raymarch per cascade |
| `res/shaders/rc_cascade_merge.fs` | Nearest merge coarse→fine |
| `res/shaders/rc_compose.fs` | Direct + `k_gi * irradiance * albedo` |
| `res/cli/debug.js` | JS CLI: pass + `rc_quality` |

Register: `register("rc", "scenes/rc.js")` in `res/boot.js`. Follow
`docs/scenes.md` describe/spawn patterns (`cube.js`).

**Material:** one shader only. Glow props use the same FS with a high
`glow` describe uniform (`ng_glow`). No lit/emissive shader split.

### Debug CLI (JS-driven)

```
? set debug.render.pass
? set debug.render.rc_quality
? set debug.render.gi_strength
set debug.render.pass albedo|normal|glow|depth|irradiance|final
set debug.render.rc_quality 0|1|2
set debug.render.gi_strength 0..8
```

## Approximate steps

### Phase 0 — Scene + materials — done
### Phase 1 — G-buffer hooks — done
### Phase 2 — Screen-space RC — done

7. Cascades 0/1/2 (4/16/64 dirs; short/mid/long intervals).
8. Fill SS raymarch into glow/albedo; miss → sky.
9. Nearest merge top→bottom; compose Direct+GI.
10. On `render:"rc"` scenes, `rc_quality≥2` enables RC; `≤1` forward only.

**Phase 2 polish:** depth-discontinuity hits, miss-gated merge (`hit_frac` alpha), compose direct↓ / `gi_strength`↑. Phase 3 still bilinear-fix later.

**Done when:** soft bounce / color bleed on `scene rc` without noise.

### Phase 3 — Quality knobs (Level 3–4)

12. Optional bilinear-fix merge (4× rays; see Osborne & Sannikov 2024).
13. More cascades / dirs; optional 2-frame amortize (“reduce demand”).
14. Ringing / parallax mitigations only if visible in `rc` scene.

### Phase 4 — Optional 3D variants (later)

15. World-space volume probes + interval extension, **or**
16. Surfel RC (surface probes; see SRC-DGI) if off-screen GI matters.

## Pipeline sketch (Level ≥2)

```
opaque G-buffer → for c = Cmax…0: march intervals
→ merge c←c+1 → irradiance from C0
→ shade = Direct + k_gi * Irradiance * albedo
→ tonemap
```

## Non-goals (v1)

- Multi-bounce radiosity loops beyond one indirect
- Denoisers / TAA dependence
- Production-scale open-world volumes

## Success criteria

- `scene rc` loads with multi-object, multi-light, glow props
- `rc_quality=0` ≈ simple direct look, stable FPS
- `rc_quality≥2` shows bounce / soft occlusion without noise
- Cascade count / dirs are the primary cost knobs

## References (read order)

1. https://radiance.wiki/
2. Sannikov WIP — https://github.com/Raikiri/RadianceCascadesPaper
3. Osborne & Sannikov 2024 — arXiv:2408.14425
4. MΛX — https://m4xc.dev/articles/fundamental-rc/
5. Jason McGhee — https://jason.today/rc
6. GM Shaders — https://mini.gmshaders.com/p/radiance-cascades
7. SimonDev — https://github.com/simondevyoutube/Shaders_RadianceCascades
8. Playground — https://radiance-cascades.com/
9. https://github.com/mxcop/src-dgi · https://tmpvar.com/poc/radiance-cascades/

<!-- agent: composer-2.5 | 2026-08-09 | Phase2 RC docs done | a6b410 -->
<!-- agent: composer-2.5 | 2026-08-09 | docs render scene declare | 41a9b6 -->
<!-- agent: composer-2.5 | 2026-08-09 | Phase2 polish docs note | 71cf57 -->
