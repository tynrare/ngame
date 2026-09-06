# Instanced drawing

All view visuals enter the scene runtime's frame queue: C/JS `draw_shape` and
`draw_label`, persistent scene models/labels, and collider-only physics bodies.
See [the draw API](draw-api.md) for usage, options and limits.

The canonical lifecycle is **frame-draw** in `src/scene/draw.c`. The renderer in
`src/client/render.c` implements step 3; `src/client/font_msdf.c` implements step 4
through its **font-msdf** glyph pipeline. Both use the same immutable commands for
world and screen scopes. Commands expire after the final pass, not after each pass.

Shapes batch by scope/pass, geometry and material ID. Scope and pass are implicit in
each collection/flush; reusable slots retain instance storage across collections.
Tint, UV and outline are per-instance attributes and never enter the batch key.
Opaque instances can group freely; transparent instances only join adjacent runs.
Labels run after shapes (after composition for RC), with depth writes disabled.

`src/client/shape.c` owns **shape-submit**, the common mesh/XY plane GPU backend.
Font code only generates atlases, lays out glyphs and forms adjacent material runs.
Each font material binds its atlas; both fonts share the compiled MSDF program.
There is no font transform-feedback stage or separate font draw machinery.

Persistent mesh commands retain graph indices for RC visibility filtering.
Immediate shapes and collider-only bodies have no persistent RC identity and skip
that filter. They enter forward and gbuffer passes but not the RC probe/BVH registry.

The mesh shaders consume `in mat4 instanceTransform`. Its final element may carry
a graph index; shaders rebuild the translation column with W=1 before transforming
vertices. Material parameters remain per model; submitted tint multiplies the
material. Physics boxes use full collider extents and spheres use collider radii.

The backend uploads packed transform/tint/UV/outline instances into one retained GPU
buffer. CPU scratch and GPU storage grow geometrically, only when required. Scene
unload releases resources and invalidates material handles. Shader and texture
resources are reference counted by path. **material-recipe** in `src/scene/assets.c`
owns registration, lookup and invalidation; the renderer owns cached GPU bindings.

Focused checks live in `tools/draw_smoke.c` (C/JS lifetimes and material handles) and
`tools/shape_smoke.c` (matching glyph transforms/styles, atlas runs, 5,200-glyph growth
and GPU cleanup), the latter runs as part of `ng_embed_smoke` under Xvfb.
