// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 411333
// agent: gpt-6-astra | 2026-09-05 | bind matching JavaScript draw options | 7e2028
#include "scene/draw.h"
#include "scene/runtime.h"
#include "vendor/duktape.h"
#include <math.h>

/** @param ctx duk_context* heap. @param obj duk_idx_t object. @param key const char* property. @param fallback float missing value. @return float finite numeric value; throws on bad input. */
static float draw_number(duk_context *ctx, duk_idx_t obj, const char *key, float fallback) {
  duk_get_prop_string(ctx, obj, key);
  float v = fallback;
  if (!duk_is_undefined(ctx, -1)) {
    v = (float)duk_require_number(ctx, -1);
    if (!isfinite(v)) duk_error(ctx, DUK_ERR_RANGE_ERROR, "%s must be finite", key);
  }
  duk_pop(ctx);
  return v;
}

/** @param ctx duk_context* heap. @param obj duk_idx_t options. @param key const char* vector property. @param out float[3] defaults/result. @return void. */
static void draw_vector(duk_context *ctx, duk_idx_t obj, const char *key, float out[3]) {
  duk_get_prop_string(ctx, obj, key);
  if (!duk_is_undefined(ctx, -1)) {
    if (!duk_is_object(ctx, -1) || duk_is_array(ctx, -1))
      duk_error(ctx, DUK_ERR_TYPE_ERROR, "draw vector/tint must be an object");
    const duk_idx_t v = duk_normalize_index(ctx, -1);
    const char *keys[] = {"x", "y", "z"};
    for (int i = 0; i < 3; i++) out[i] = draw_number(ctx, v, keys[i], out[i]);
  }
  duk_pop(ctx);
}

/** @param ctx duk_context* heap. @param idx duk_idx_t optional options. @return NgDrawOptions checked pose/style. */
static NgDrawOptions draw_options_read(duk_context *ctx, duk_idx_t idx) {
  NgDrawOptions o = ng_draw_options();
  if (duk_is_null_or_undefined(ctx, idx)) return o;
  if (!duk_is_object(ctx, idx) || duk_is_array(ctx, idx))
    duk_error(ctx, DUK_ERR_TYPE_ERROR, "draw options must be an object");
  duk_get_prop_string(ctx,idx,"material");
  if(!duk_is_undefined(ctx,-1)) {
    if(duk_is_string(ctx,-1)) o.material=ng_material_lookup(duk_get_string(ctx,-1));
    else {
      double id=duk_require_number(ctx,-1);
      if(!isfinite(id) || id<1 || id>UINT32_MAX || floor(id)!=id) duk_error(ctx,DUK_ERR_RANGE_ERROR,"invalid material handle");
      o.material.id=(uint32_t)id;
    }
    if(!ng_material_get(o.material)) duk_error(ctx,DUK_ERR_REFERENCE_ERROR,"unknown or stale material");
  }
  duk_pop(ctx);
  draw_vector(ctx, idx, "position", o.position);
  draw_vector(ctx, idx, "rotation", o.rotation);
  duk_get_prop_string(ctx, idx, "scale");
  if (duk_is_number(ctx, -1)) {
    o.scale[0] = o.scale[1] = o.scale[2] = (float)duk_get_number(ctx, -1);
    duk_pop(ctx);
  } else {
    duk_pop(ctx);
    draw_vector(ctx, idx, "scale", o.scale);
  }
  for (int i = 0; i < 3; i++)
    if (!isfinite(o.scale[i]) || o.scale[i] <= 0)
      duk_error(ctx, DUK_ERR_RANGE_ERROR, "draw scale must be positive and finite");
  duk_get_prop_string(ctx, idx, "scope");
  if (!duk_is_undefined(ctx, -1)) {
    const char *scope = duk_require_string(ctx, -1);
    int id = mod_scene_assets_lookup_scope(scope);
    if (id < 0) duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "unknown draw scope: %s", scope);
    o.scope_id = (uint8_t)id;
  }
  duk_pop(ctx);
  duk_get_prop_string(ctx, idx, "tint");
  if (!duk_is_undefined(ctx, -1)) {
    if (!duk_is_object(ctx, -1) || duk_is_array(ctx, -1))
      duk_error(ctx, DUK_ERR_TYPE_ERROR, "draw vector/tint must be an object");
    const duk_idx_t tint = duk_normalize_index(ctx, -1);
    const char *keys[] = {"r", "g", "b"};
    for (int i = 0; i < 3; i++) {
      float v = draw_number(ctx, tint, keys[i], 255);
      if (v < 0 || v > 255) duk_error(ctx, DUK_ERR_RANGE_ERROR, "draw tint must be 0..255");
      o.tint[i] = (uint8_t)v;
    }
  }
  duk_pop(ctx);
  o.size = draw_number(ctx, idx, "size", o.size);
  o.outline = draw_number(ctx, idx, "outline", o.outline);
  if (o.size <= 0 || o.outline < 0 || o.outline > 0.5f)
    duk_error(ctx, DUK_ERR_RANGE_ERROR, "draw size must be positive; outline must be 0..0.5");
  return o;
}

/** @param ctx duk_context* (name, options?). @return duk_ret_t one acceptance boolean. */
static duk_ret_t bind_draw_shape(duk_context *ctx) {
  const char *name = duk_require_string(ctx, 0);
  NgDrawOptions o = draw_options_read(ctx, 1);
  duk_push_boolean(ctx, mod_scene_runtime_active() == &g_scene_view && ng_draw_shape(name, &o));
  return 1;
}

/** @param ctx duk_context* (font, text, options?). @return duk_ret_t one acceptance boolean. */
static duk_ret_t bind_draw_label(duk_context *ctx) {
  const char *font = duk_require_string(ctx, 0);
  const char *text = duk_require_string(ctx, 1);
  NgDrawOptions o = draw_options_read(ctx, 2);
  duk_push_boolean(ctx, mod_scene_runtime_active() == &g_scene_view && ng_draw_label(font, text, &o));
  return 1;
}

/** @param ctx duk_context* material name. @return duk_ret_t cached handle, zero for missing name. */
static duk_ret_t bind_material(duk_context *ctx) {
  duk_push_uint(ctx,ng_material_lookup(duk_require_string(ctx,0)).id); return 1;
}

/** @param ctx duk_context* scene heap. @return void; installs global.draw_shape and global.draw_label. */
void ng_draw_bind(duk_context *ctx) {
  duk_push_global_object(ctx);
  duk_push_c_function(ctx,bind_material,1);
  duk_put_prop_string(ctx,-2,"material");
  duk_push_c_function(ctx, bind_draw_shape, 2);
  duk_put_prop_string(ctx, -2, "draw_shape");
  duk_push_c_function(ctx, bind_draw_label, 3);
  duk_put_prop_string(ctx, -2, "draw_label");
  duk_pop(ctx);
}
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 411333
// agent: gpt-6-astra | 2026-09-05 | bind matching JavaScript draw options | 7e2028
