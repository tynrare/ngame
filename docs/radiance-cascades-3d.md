<!-- agent: composer-2.5 | 2026-08-09 | Phase1 CLI gbuf docs | 87c6db -->
# Radiance Cascades (3D) — North Star

Goal: a **performance-scalable** GI path for ngame, with a clean JS scene
(`res/scenes/rc.js`) and classical material shaders, degrading to simple
direct lighting when cascades are off or cheap.

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
Knob: `rc_quality` ∈ {0…4}.

## Target deliverables

| Path | Role |
|------|------|
| `docs/radiance-cascades-3d.md` | This north star |
| `res/scenes/rc.js` | Test scene: multi-mesh, lights, glow props |
| `res/shaders/rc.fs` | One material: tint, roughness, metalness, **glow** |
| `res/shaders/rc_gbuf.fs` | G-buffer debug write (albedo/normal/glow/depth) |
| `res/cli/debug.js` | JS CLI: `set debug.render.pass` |
| `res/shaders/rc_compose.fs` | Later: direct + cascade irradiance (+ bloom) |
| (later) cascade fill / merge passes | Screen-space RC first |

Register: `register("rc", "scenes/rc.js")` in `res/boot.js`. Follow
`docs/scenes.md` describe/spawn patterns (`cube.js`).

**Material:** one shader only. Glow props use the same FS with a high
`glow` describe uniform (`ng_glow`). No lit/emissive shader split.

```javascript
global.describe("shader", "mat_s", {
  fragment: "shaders/rc.fs",
  vertex: "shaders/mesh.vs",
  tint: { r, g, b },
  glow: { r, g, b },       // 0 = none; bright = self-lit
  roughness: 0.5,
  metalness: 0.0,
});
```

### Debug CLI (JS-driven)

Tab console → cmds go to `res/bus.js` (+ `res/cli/*.js`). Help is hierarchical:

```
?                              # top-level cmds
? set                          # set subtree
? set debug.render.pass        # values + current
set debug.render.pass albedo   # final|albedo|normal|glow|depth
```

## Approximate steps

### Phase 0 — Scene + materials (no RC) — done

1. `rc.js` + `rc.fs` + describe uniforms (`glow` / roughness / metalness).
2. `scene rc` multi-object lit stage with glow props.

### Phase 1 — G-buffer hooks — done

4. RTs for albedo / normal / glow / depth via `rc_gbuf.fs` + `mesh.vs`.
5. Fullscreen blit selected by `set debug.render.pass KEY`.
6. CLI help/set in JS (`bus.js`, `cli/debug.js`); C only binds set/get/status.

**Done when:** buffers readable via debug pass CLI.

### Phase 2 — Screen-space RC (Level 2)

7. Define cascade params: C0 probe spacing, ray dirs, interval lengths;
   scale ~2×–4× angular / ½ spatial per cascade (paper / GMShaders).
8. Fill cascades: short SS raymarch into glow/albedo; miss → sky/ambient.
9. Merge top→bottom (nearest first); sample C0 irradiance in compose.
10. Shade: `Direct(L,N,V,rough,metal) + k_gi * Irradiance * albedo`.
11. Expose `rc_quality`; Level ≤1 skips cascade passes entirely.

**Done when:** soft bounce / color bleed without noise at Level 2.

### Phase 3 — Quality knobs (Level 3–4)

12. Optional bilinear-fix merge (4× rays; see Osborne & Sannikov 2024).
13. More cascades / dirs; optional 2-frame amortize (“reduce demand”).
14. Ringing / parallax mitigations only if visible in `rc` scene.

**Done when:** Level 4 is the demo look; Level 0 remains the fallback.

### Phase 4 — Optional 3D variants (later)

15. World-space volume probes + interval extension, **or**
16. Surfel RC (surface probes; see SRC-DGI) if off-screen GI matters.

Defer until screenspace ladder is solid. Do not start with full 3D volume C0
unless the test volume is tiny.

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

1. https://radiance.wiki/ — hub, variants, fixes
2. Sannikov WIP — https://github.com/Raikiri/RadianceCascadesPaper
3. Osborne & Sannikov 2024 — arXiv:2408.14425 (bilinear / parallax)
4. MΛX fundamentals — https://m4xc.dev/articles/fundamental-rc/
5. Jason McGhee — https://jason.today/rc (+ https://jason.today/gi)
6. GM Shaders — https://mini.gmshaders.com/p/radiance-cascades
   + part 2 — https://mini.gmshaders.com/p/radiance-cascades2
   + code — https://github.com/Yaazarai/GMShaders-Radiance-Cascades
7. SimonDev — https://github.com/simondevyoutube/Shaders_RadianceCascades
8. Playground — https://radiance-cascades.com/
9. 3D experiments — https://github.com/mxcop/src-dgi (surfel)
   · https://tmpvar.com/poc/radiance-cascades/ (cost tables)

<!-- agent: composer-2.5 | 2026-08-09 | Phase1 CLI gbuf docs | 87c6db -->
