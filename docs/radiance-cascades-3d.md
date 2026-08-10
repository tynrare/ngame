<!-- agent: composer-2.5 | 2026-08-10 | doc brighter unity look | bd4e64 -->
# Radiance Cascades (3D) — North Star

Goal: **dynamic, deterministic GI** (WebGL2/GLES3). Quality = **cost only**.
Product path = **world-space Radiance Cascades**.  
Compose: `ambient×1 + directional×1 + gi_strength×1·kd·irr + glow` (brightness via bases + fill bounce).

## What ships (aligned with code)

| Piece | Implementation |
|-------|----------------|
| Gbuffer | albedo+**rough(A)** / normal+**metal(A)** / glow / depth (`RGB=UVW`, `A=inside`) |
| Clip | Fixed cube on **look-at**; large hysteresis |
| Geometry (**6.2**) | Analytic SDF prims (`tex_prim`) |
| Fill | SDF march; hit = `emit·EMIT + albedo·bounce(rough,metal)` |
| Merge / SH / Resolve | T-merge → L1 SH → trilinear `texelFetch` |
| Compose | `AMBIENT=1`, `DIRECTIONAL=1`; GI via `gi_strength` (default 1); gbuf rough/metal |
| Quality | Scales **probe_n / dirs / cascades / steps** only |

**Materials (revalidated):**
| Prop | Direct (`rc.fs` / compose) | Fill bounce (GI inject) |
|------|---------------------------|-------------------------|
| **Roughness** | Shininess ↑ when smooth; spec dimmed when rough | More Lambert bounce when rough (`mix(0.45,1,rough)`) |
| **Metalness** | Diffuse ↓; spec tint → albedo | Diffuse bounce × `(1−metal)` (specular not marched) |
| **Specular** | Blinn-Phong on compose/direct only | **Not** in cascade hit (L1 SH is diffuse-ish) |
| **Glow/emit** | Screen add in compose | `EMIT_BOOST` in fill |

**Look knobs:** `EMIT_BOOST≈6`, `BOUNCE≈1.1` (color bleed restored); compose GI `×~1.05` with `kd` metal cut so sphere bottoms pick up floor without washing metals.  
**Gateway:** `src/client/render_rc_ws.c` (`Flow id: rc-ws`).

## Pipeline

```
look-at cube → gbuf (albedo+rough, n+metal, glow, UVW)
→ rebuild_prims → SDF fill → T-merge → SH → resolve → compose
```

## Phases

| Phase | Goal | State |
|------:|------|--------|
| 0–5 | Scene → WS RC + SH | **done** |
| **6.1–6.2** | Look-at clip + SDF prims | **done** |
| **Look** | Bounce + PBR compose | **landed** (tune via `gi_strength`) |
| **6.2.3** | March AABB / classic SDF opts | **next** |
| **6.3** | SVO | **later** |
| **B** | Sparse / clipmap | **after look + 6.2.3** |

### Tracks

| Track | Work | State |
|-------|------|--------|
| **A** | Clip + SDF + PBR look | **done** |
| **A** | Classic SDF march opts (6.2.3+) | **next** |
| **A** | SVO | **6.3 later** |
| **B** | Sparse / clipmap | **after A opts** |

---

## Look (current)

| Knob | Default | Role |
|------|---------|------|
| `AMBIENT` / `DIRECTIONAL` | **1** | Scales only — leave at 1 |
| `AMBIENT_ALBEDO` | **0.22** | Brightness under ambient=1 |
| Light `c0`/`c1` | ~1.15 / fill | Brightness under directional=1 |
| `ng_gi_strength` | **1** | `gi = kd × irr × this` |
| Fill `BOUNCE` / `E_LIT` | ~1.65 / ~1.35 | Floor→underside; exitance at exposure 1 |
| Fill `EMIT_BOOST` | ~6 | Glow inject |

Sphere bottoms are N·L≈0 — only ambient + down-hemisphere GI. Raise bounce/`E_LIT` (not ambient scale) for floor light.

---

## 6.2.3 + classic SDF opts (next)

1. Bound sphere / AABB per prim (skip SDF on miss)  
2. Scene ∩ clip bound  
3. Conservative `min(d, dt_max)` near hits  
4. Hit/miss step budget  
5. Pack bound radius in prim row  
6. Grid/BVH candidate lists (many prims)  
7. **6.3** SVO empty-skip  
8. No triangle bake / no SSBO requirement on WebGL2  

---

## Further plans

| Priority | Item |
|---------:|------|
| 1 | Verify look (red floor bleed, sphere undersides, metal vs rough highlights) |
| 2 | **6.2.3** + SDF opts |
| 3 | Grid/BVH for many prims |
| 4 | **Track B** sparse/clipmap |
| 5 | **6.3** SVO |
| 6 | Optional: GGX compose; specular lobe in higher SH (hard) |
| 7 | Bloom / L2 SH / bilinear-fix merge |
| 8 | SS shelved until WS look OK |

## Anti-patterns

- Crushing `BOUNCE` until color bleed dies  
- Ignoring packed roughness in fill  
- Compose without rough/metal (flat spheres, no spec)  
- Cam-follow clip; clamped UVW faces  
- Track B before march opts / look OK  

## References

- https://radiance.wiki/ · jason.today/rc · Split RC arXiv:2607.20384 · arXiv:2408.14425  

<!-- agent: composer-2.5 | 2026-08-10 | doc brighter unity look | bd4e64 -->
