# Drawing from C and JavaScript

Use `draw_shape(name, options?)` and `draw_label(font, text, options?)` in a scene,
wired module, or entity's `draw()` method. The view invokes `draw()` once per rendered
frame, including for remote entities. Draw calls copy values into a reusable frame
queue; they do not create entities, bodies, or network state. Submit again next frame.
Run `scene draw` in the console for the example in `res/scenes/draw.js`.

```js
Scene.prototype.draw = function () {
  global.draw_shape("box_shape", {
    position: { x: 1, y: 0, z: 0 },
    rotation: { y: global.now() },
    tint: { r: 100, g: 180, b: 255 }
  });
  global.draw_label("sans", "Hello", {
    scope: "screen", position: { x: 24, y: 80 }, size: 24
  });
};
```

Describe geometry with the existing `describe("shape", name, ...)` physics API,
or pass a described mesh model. Models win name collisions; `"shape:box_shape"`
explicitly selects the physics recipe. Box half-extents become full visual dimensions;
sphere radius is unchanged. Bodies without a model automatically draw their collider
through this same API. Existing modeled bodies retain their visual models.

Labels use a described font (`describe("font", "sans", {src: "fonts/LiberationSans-Regular.ttf"})`).
Persistent label entities and immediate labels share the same MSDF renderer and
font cache. Different font sources receive separate lazily generated atlases.

| Option | Default | Meaning |
|---|---|---|
| `material` | Geometry default | Shape material name or cached `global.material(name)` handle |
| `position` | `{x:0,y:0,z:0}` | World coordinates, or screen pixels in an ortho scope |
| `rotation` | `{x:0,y:0,z:0}` | Euler radians; world labels rotate within their billboard basis |
| `scale` | `1` | Positive number or `{x,y,z}` multiplier |
| `tint` | `{r:255,g:255,b:255}` | RGB bytes; multiplies model material, sets label color |
| `scope` | Scene's first bound scope | Named world or screen scope |
| `size` | `16` | Label height in world units or pixels; use e.g. `0.3` for world labels |
| `outline` | `0` | MSDF outline distance, `0..0.5` |

Missing vector components use defaults. Default shape materials are opaque. Labels accept at most
255 bytes; the existing font engine supports printable ASCII. Unsupported glyphs are skipped. Glyphs use the shared XY plane backend.
Shapes batch by scope, pass, geometry and material ID; tint, UV and outline stay per instance.
Opaque batches may reorder; transparent shapes and labels batch adjacent compatible runs.
Labels render after opaque shapes with depth writes disabled, in submission order.
There is no transparency sorting between shapes and labels.

C uses the same names and options through `scene/draw.h`:

```c
mod_scene_runtime_use_view();
NgDrawOptions draw = ng_draw_options();
draw.position[0] = 1.0f;
draw.tint[0] = 100;
bool accepted = ng_draw_shape("box_shape", &draw);
draw.scope_id = (uint8_t)mod_scene_assets_lookup_scope("screen");
draw.size = 24;
accepted = ng_draw_label("sans", "Hello", &draw);
```

Initialize options with `ng_draw_options()`; a zeroed struct has invalid scale/size.
C calls target the active runtime and must run on the render thread before rendering.
Both APIs return `false` for unknown/wrong asset types, oversized labels, or a full
queue. JS throws helpful errors for malformed options and unknown scopes. Server-side
JS draw calls return `false`; drawing does not enter simulation or rollback.

The fixed queue holds 4096 commands (persistent and transient combined). Failed
submissions leave existing commands intact. `ng_draw_queue()` exposes the accepted
count and rejected capacity/option count during the frame. The renderer consumes and
clears it after all passes; scene unload clears it too. Mesh batch storage grows as
needed and is reused, with at most 4096 geometry/material runs per pass. The shared GPU
instance buffer grows only when capacity increases. Scene unload frees GPU and CPU storage.

Transient shapes participate in forward and gbuffer rendering. They do not acquire
persistent identities in the RC probe/BVH system, so they do not become GI occluders
or emitters there. World labels also render after RC composition, as a visual overlay;
that composed pass does not carry scene depth for label occlusion. Supported scopes
remain the renderer's first world scope and first ortho scope.

The canonical lifecycle is **frame-draw** in `src/scene/draw.c`; glyph processing is
**font-msdf** in `src/client/font_msdf.c`. See [instanced drawing](instanced-draw.md).

## Materials

```js
global.describe("material", "paint", {
  shader: "my_shader", albedo: "textures/albedo.png",
  blend: false, depth_test: true, depth_write: true
});
var paint = global.material("paint");
global.draw_shape("my_model", { material: paint, tint: {r:200,g:120,b:80} });
```

`albedo` binds texture unit 0. `blend` enables ordinary alpha blending; `depth_write`
defaults to the inverse of `blend`. Shader and texture paths share cached GPU resources.
Recipes are immutable; dispose a material before describing a replacement. C uses
`NgMaterialHandle` and `ng_material_lookup(name)`; `ng_material_get(handle)` returns
NULL after disposal or scene reset. Cached JS numeric handles also become invalid.
Models and fonts register default materials under their own names. Labels always
select their font's MSDF material: atlas UVs select the glyph, tint supplies its color.

`scene fonts` compares immediate and created labels using both bundled fonts in
world and screen coordinates. Each pair uses identical style and rotation, with a
translation separating the two rows. Its immediate flash is submitted only on frame
one; the created survivor stays until scene stop explicitly despawns it.
