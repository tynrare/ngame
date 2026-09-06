// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0d5358
// agent: gpt-6-astra | 2026-09-05 | test draw queue and API contracts | e00474
#include "scene/draw.h"
#include "scene/runtime.h"
#include "vendor/duktape.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "draw smoke line %d: %s\n", __LINE__, #x); return false; } } while (0)

/** @return bool C/JS queue, collider dimensions, graph routing and runtime isolation hold. */
bool ng_draw_smoke_test(void) {
  mod_scene_runtime_use_view();
  mod_scene_assets_reset();
  mod_scene_graph_reset();
  ng_draw_reset();
  NgSceneViewMeta view = {.valid = true, .camera_mode = NG_SCENE_CAM_FIXED};
  CHECK(mod_scene_assets_describe_scope("world", &view));
  CHECK(mod_scene_physics_describe_shape("box", "box", 1, 2, 3, 1, 0.3f, false));
  CHECK(mod_scene_physics_describe_shape("ball", "sphere", 0.75f, 0.75f, 0.75f, 1, 0.3f, false));
  CHECK(mod_scene_assets_describe_font("sans", "fonts/LiberationSans-Regular.ttf"));
  CHECK(mod_scene_assets_describe_shader("flat","shaders/flat.fs","shaders/mesh.vs",255,255,255,false,0,0,0,false,1,0));
  CHECK(ng_material_describe("paint","flat",NULL,false,true,true));
  NgMaterialHandle paint=ng_material_lookup("paint");
  CHECK(paint.id && ng_material_get(paint));
  CHECK(ng_material_lookup("paint").id==paint.id && !ng_material_lookup("missing").id);
  NgDrawOptions o = ng_draw_options();
  CHECK(o.scale[0] == 1 && o.tint[2] == 255);
  o.position[0] = 7;
  o.scale[1] = 2;
  CHECK(ng_draw_shape("box", &o));
  const NgDrawQueue *q = ng_draw_queue();
  CHECK(q->count == 1 && q->commands[0].options.position[0] == 7);
  CHECK(q->commands[0].options.scale[0] == 2 && q->commands[0].options.scale[1] == 8);
  CHECK(q->commands[0].options.scale[2] == 6);
  CHECK(ng_draw_shape("ball", NULL));
  CHECK(q->commands[1].options.scale[0] == 0.75f);
  char label[] = "copied";
  CHECK(ng_draw_label("sans", label, NULL));
  label[0] = 'X';
  CHECK(!strcmp(q->commands[2].text, "copied"));
  CHECK(!ng_draw_shape("missing", NULL));
  CHECK(!ng_draw_shape("sans", NULL));
  CHECK(!ng_draw_label("box", "bad", NULL));
  o.material=(NgMaterialHandle){UINT32_MAX}; CHECK(!ng_draw_shape("box",&o));
  o.material=paint; CHECK(ng_draw_shape("box",&o));
  CHECK(q->commands[q->count-1].options.material.id==paint.id);
  mod_scene_runtime_active()->draw.count--;
  o.material=(NgMaterialHandle){0};
  o.position[0] = NAN;
  CHECK(!ng_draw_shape("box", &o));
  o = ng_draw_options();
  o.scope_id = 255;
  CHECK(!ng_draw_shape("box", &o));
  char long_text[NG_DRAW_TEXT_MAX + 1];
  memset(long_text, 'a', sizeof(long_text));
  long_text[NG_DRAW_TEXT_MAX] = 0;
  CHECK(!ng_draw_label("sans", long_text, NULL));
  CHECK(q->count == 3);

  duk_context *js = duk_create_heap_default();
  CHECK(js);
  ng_draw_bind(js);
  CHECK(duk_peval_string(js,
    "if (!draw_shape('box', {position:{x:4}, scale:{y:3}})) throw Error('submit');"
    "if (!draw_label('sans','JS',{size:22,tint:{r:10}})) throw Error('label');"
    "if (draw_shape('unknown')) throw Error('unknown accepted');"
    "var cached=material('paint'); if(!cached) throw Error('lookup');"
    "if (!draw_shape('box',{material:cached}) || !draw_shape('box',{material:'paint'})) throw Error('override');"
    "var bad = [{material:0},{material:4294967295},{material:'missing'},{scale:0},{position:{x:NaN}},{scope:'missing'},{tint:{r:256}},"
    " {position:4},{scale:[]},{outline:1}];"
    "for (var i=0;i<bad.length;i++) {var caught=false;"
    " try {draw_shape('box',bad[i]);} catch(e) {caught=true;}"
    " if(!caught) throw Error('invalid options accepted');}"
    ) == DUK_EXEC_SUCCESS);
  duk_pop(js);
  CHECK(q->count == 7 && q->commands[3].options.scale[1] == 12);
  CHECK(q->commands[4].options.tint[0] == 10 && q->commands[4].options.tint[1] == 255);
  mod_scene_runtime_use_server();
  ng_draw_reset();
  CHECK(duk_peval_string(js, "draw_shape('box')") == DUK_EXEC_SUCCESS);
  CHECK(!duk_get_boolean(js, -1));
  duk_pop(js);
  CHECK(ng_draw_queue()->count == 0 && q->count == 7);
  mod_scene_runtime_use_view();
  duk_destroy_heap(js);
  ng_draw_reset();

  CHECK(mod_scene_physics_describe_body("body", "static", "box", false));
  CHECK(mod_scene_graph_describe("entity", "body_e", NG_SYNC_SHARED, "", "body", -1));
  float pos[3] = {3, 4, 5}, rot[3] = {0};
  CHECK(mod_scene_graph_spawn("body_e", 1, "body", pos, rot, 1, NULL, -1));
  CHECK(mod_scene_graph_describe("entity", "label_e", NG_SYNC_SHARED, "sans", "", -1));
  int handle = mod_scene_graph_spawn("label_e", 2, "label", pos, rot, 1, NULL, -1);
  CHECK(handle);
  NgSceneInst *inst = mod_scene_graph_inst_by_handle(handle);
  strcpy(inst->text, "persistent");
  inst->text_size = 0.3f;
  ng_draw_collect_graph(0);
  CHECK(q->count == 2 && q->commands[0].kind == NG_DRAW_SHAPE);
  CHECK(q->commands[0].options.scale[1] == 4);
  CHECK(q->commands[1].kind == NG_DRAW_LABEL && !strcmp(q->commands[1].text, "persistent"));
  NgDrawCommand persistent=q->commands[1];
  ng_draw_reset(); CHECK(ng_draw_label("sans","persistent",&persistent.options));
  CHECK(!memcmp(&q->commands[0].options,&persistent.options,sizeof(persistent.options)));
  ng_draw_reset(); ng_draw_collect_graph(0); CHECK(q->count==2);
  strcpy(inst->text,"updated"); ng_draw_reset(); ng_draw_collect_graph(0);
  CHECK(!strcmp(q->commands[1].text,"updated"));
  CHECK(mod_scene_graph_despawn(handle)); ng_draw_reset(); ng_draw_collect_graph(0); CHECK(q->count==1);
  ng_draw_reset();
  for (int i = 0; i < NG_DRAW_MAX; i++) CHECK(ng_draw_shape("box", NULL));
  CHECK(!ng_draw_label("sans", "overflow", NULL));
  CHECK(q->count == NG_DRAW_MAX && q->rejected == 1);
  ng_draw_reset();
  CHECK(q->count == 0 && q->rejected == 0 && ng_draw_shape("box", NULL));
  ng_draw_reset();
  mod_scene_physics_reset();
  mod_scene_graph_reset();
  mod_scene_assets_reset();
  CHECK(!ng_material_get(paint));
  puts("DRAW_SMOKE ok");
  return true;
}
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 0d5358
// agent: gpt-6-astra | 2026-09-05 | test draw queue and API contracts | e00474
