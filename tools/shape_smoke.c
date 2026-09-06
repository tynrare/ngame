// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | dc417e
#include "client/font_msdf.h"
#include "client/render.h"
#include "client/shape.h"
#include "engine/ng_session.h"
#include "scene/draw.h"
#include "scene/runtime.h"
#include <raymath.h>
#include <rlgl.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "shape smoke %d: %s\n", __LINE__, #x);                   \
      return false;                                                            \
    }                                                                          \
  } while (0)
/** @return NgShapeStats one renderer frame, consuming the current queue. */
static NgShapeStats render_frame(void) {
  ng_shape_stats(true);
  BeginDrawing();
  NgMsg draw = {.kind = NG_MSG_DRAW};
  mod_render_ops()->on_msg(&draw, mod_render_ctx());
  EndDrawing();
  return ng_shape_stats(true);
}
/** @return bool mesh material batching and override resources are honored. */
static bool material_batches(void) {
  mod_scene_assets_reset();
  mod_scene_graph_reset();
  ng_draw_reset();
  NgSceneViewMeta view = {.valid = true,
                          .camera_mode = NG_SCENE_CAM_FIXED,
                          .cam_pos = {0, 0, 6},
                          .cam_fovy = 45};
  CHECK(mod_scene_assets_describe_scope("world", &view));
  CHECK(mod_scene_assets_describe_mesh("mesh", "cube", 1, 1, 1));
  CHECK(mod_scene_assets_describe_shader("flat", "shaders/flat.fs",
                                         "shaders/mesh.vs", 255, 255, 255,
                                         false, 0, 0, 0, false, 1, 0));
  CHECK(mod_scene_assets_describe_shader("sphere", "shaders/sphere.fs",
                                         "shaders/mesh.vs", 255, 255, 255,
                                         false, 0, 0, 0, false, 1, 0));
  CHECK(mod_scene_assets_describe_model("model", "mesh", "flat", NULL));
  CHECK(ng_material_describe("paint", "sphere", NULL, false, true, true));
  CHECK(ng_material_describe("alpha", "flat", NULL, true, true, false));
  CHECK(ng_material_describe("beta", "flat", NULL, true, true, false));
  CHECK(mod_render_ops()->init(mod_render_ctx()));
  NgSessionState session = {.scene_id = "material-test"};
  mod_render_apply_session(&session);
  NgDrawOptions o = ng_draw_options();
  CHECK(ng_draw_shape("model", &o));
  NgShapeStats original = render_frame();
  CHECK(original.draws == 1 && original.instances == 1);
  o.material = ng_material_lookup("paint");
  CHECK(ng_draw_shape("model", &o));
  NgShapeStats override = render_frame();
  CHECK(override.draws == 1 && override.digest != original.digest);
  CHECK(ng_draw_shape("model", &o));
  CHECK(render_frame().digest == override.digest);
  CHECK(ng_draw_shape("model", &o));
  o.tint[0] = 10;
  o.outline = 0.2f;
  CHECK(ng_draw_shape("model", &o));
  NgShapeStats tinted = render_frame();
  CHECK(tinted.draws == 1 && tinted.instances == 2);
  o.material = ng_material_lookup("alpha");
  CHECK(ng_draw_shape("model", &o));
  o.material = ng_material_lookup("beta");
  CHECK(ng_draw_shape("model", &o));
  o.material = ng_material_lookup("alpha");
  CHECK(ng_draw_shape("model", &o));
  CHECK(render_frame().draws == 3);
  mod_render_ops()->shutdown(mod_render_ctx());
  CHECK(ng_shape_stats(false).capacity == 0);
  mod_scene_assets_reset();
  ng_draw_reset();
  return true;
}
/** @return bool label layout, atlas runs, buffer growth and cleanup agree for
 * both lifetimes. */
bool ng_shape_smoke_test(void) {
  mod_scene_runtime_use_view();
  mod_scene_assets_reset();
  mod_scene_graph_reset();
  ng_draw_reset();
  NgSceneViewMeta screen = {.valid = true, .camera_mode = NG_SCENE_CAM_ORTHO};
  CHECK(mod_scene_assets_describe_scope("screen", &screen));
  CHECK(mod_scene_assets_describe_font("sans",
                                       "fonts/LiberationSans-Regular.ttf"));
  CHECK(mod_scene_assets_describe_font("rubik", "fonts/Rubik-ExtraBold.ttf"));
  NgDrawOptions o = ng_draw_options();
  o.scope_id = mod_scene_assets_lookup_scope("screen");
  o.size = 18;
  o.outline = 0.12f;
  o.position[0] = 2;
  o.position[1] = 3;
  o.rotation[2] = 0.2f;
  o.tint[0] = 123;
  CHECK(ng_draw_label("sans", "Matching glyphs", &o));
  ng_shape_stats(true);
  mod_font_msdf_draw_screen(o.scope_id);
  NgShapeStats immediate = ng_shape_stats(true);
  CHECK(immediate.draws == 1 && immediate.instances == 14);
  ng_draw_reset();
  CHECK(mod_scene_graph_describe("entity", "label", NG_SYNC_SHARED, "sans", "",
                                 -1));
  int h = mod_scene_graph_spawn("label", 1, "label", o.position, o.rotation, 1,
                                NULL, o.scope_id);
  CHECK(h);
  NgSceneInst *inst = mod_scene_graph_inst_by_handle(h);
  CHECK(inst);
  strcpy(inst->text, "Matching glyphs");
  inst->text_size = o.size;
  inst->text_outline = o.outline;
  inst->tint_r = o.tint[0];
  inst->tint_g = o.tint[1];
  inst->tint_b = o.tint[2];
  ng_draw_collect_graph(0);
  mod_font_msdf_draw_screen(o.scope_id);
  NgShapeStats created = ng_shape_stats(true);
  CHECK(created.instances == immediate.instances &&
        created.digest == immediate.digest);
  Camera3D camera = {
      .position = {2, 1, 6}, .target = {0, 0, 0}, .up = {0, 1, 0}, .fovy = 45};
  BeginMode3D(camera);
  ng_shape_stats(true);
  mod_font_msdf_draw_world(&camera, o.scope_id);
  NgShapeStats world_created = ng_shape_stats(true);
  ng_draw_reset();
  CHECK(ng_draw_label("sans", "Matching glyphs", &o));
  mod_font_msdf_draw_world(&camera, o.scope_id);
  NgShapeStats world_immediate = ng_shape_stats(true);
  EndMode3D();
  CHECK(world_created.digest == world_immediate.digest &&
        world_created.instances == 14);
  ng_draw_reset();
  ng_draw_collect_graph(0);
  CHECK(ng_draw_queue()->count == 1);
  CHECK(mod_scene_graph_despawn(h));
  ng_draw_reset();
  ng_draw_collect_graph(0);
  CHECK(!ng_draw_queue()->count);
  for (int i = 0; i < 200; i++) {
    o.tint[0] = (unsigned char)i;
    o.outline = i % 2 ? 0.1f : 0;
    CHECK(ng_draw_label("sans", "abcdefghijklmnopqrstuvwxyz", &o));
  }
  mod_font_msdf_draw_screen(o.scope_id);
  NgShapeStats grown = ng_shape_stats(true);
  CHECK(grown.draws == 1 && grown.instances == 5200 && grown.capacity >= 5200);
  ng_draw_reset();
  CHECK(ng_draw_label("sans", "A", &o));
  CHECK(ng_draw_label("rubik", "B", &o));
  CHECK(ng_draw_label("sans", "C", &o));
  mod_font_msdf_draw_screen(o.scope_id);
  CHECK(ng_shape_stats(true).draws == 3);
  ng_draw_reset();
  mod_font_msdf_shutdown();
  CHECK(ng_shape_stats(false).capacity == 0);
  mod_scene_graph_reset();
  mod_scene_assets_reset();
  CHECK(material_batches());
  puts("SHAPE_SMOKE ok");
  return true;
}
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | dc417e
