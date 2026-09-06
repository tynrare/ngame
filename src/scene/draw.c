// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 21e0c6
// agent: gpt-6-astra | 2026-09-05 | implement shared frame draw queue | 583979
/* Flow ID: frame-draw (canonical owner).
 * Scope index: host.c dispatches callbacks; render.c consumes shape batches;
 * font_msdf.c owns font-msdf (atlas/layout); shape.c owns shape-submit GPU draws.
 * 1) C callers / view draw callbacks → ng_draw_shape/label → copied frame commands.
 * 2) renderer → collect graph → persistent models, labels and model-less bodies join queue.
 * 3) renderer → filter scope/visibility → mesh/material batches per forward/gbuffer pass.
 * 4) font layout → adjacent material runs → shared XY planes after opaque/composition.
 * 5) renderer / scene unload → reset → no transient command survives the frame/scene.
 * Invariants: queues belong to each runtime; no GPU or allocation in submission;
 * callbacks run once, never per pass or simulation tick. Reject invalid/full submissions
 * without modifying existing commands. Models win name collisions; shape: forces physics.
 * Transient shapes enter the gbuffer but have no persistent RC probe/BVH identity.
 */
#include "scene/draw.h"
#include "scene/runtime.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

const NgDrawQueue *ng_draw_queue(void) { return &mod_scene_runtime_active()->draw; }
/** @return void; retain queue storage while dropping frame contents. */
void ng_draw_reset(void) {
  NgDrawQueue *q = &mod_scene_runtime_active()->draw;
  q->count = 0;
  q->rejected = 0;
}

/** @return NgDrawOptions identity transform and documented defaults. */
NgDrawOptions ng_draw_options(void) {
  return (NgDrawOptions){.scale = {1, 1, 1}, .tint = {255, 255, 255},
    .scope_id = (uint8_t)mod_scene_assets_default_scope_id(), .size = 16};
}

/** @param o const NgDrawOptions* candidate. @return bool finite, positive transform and valid scope. */
static bool draw_options_valid(const NgDrawOptions *o) {
  const NgSceneScopeDesc *scope = mod_scene_assets_scope_at(o->scope_id);
  if (!scope || !scope->alive) return false;
  if(o->material.id && !ng_material_get(o->material)) return false;
  for (int i = 0; i < 3; i++) {
    if (!isfinite(o->position[i]) || !isfinite(o->rotation[i]) ||
        !isfinite(o->scale[i]) || o->scale[i] <= 0) return false;
  }
  return isfinite(o->size) && o->size > 0 && isfinite(o->outline) &&
         o->outline >= 0 && o->outline <= 0.5f;
}

/** @param c const NgDrawCommand* fully resolved value. @return bool copied or rejected atomically. */
static bool draw_submit(const NgDrawCommand *c) {
  NgDrawQueue *q = &mod_scene_runtime_active()->draw;
  if (q->count == NG_DRAW_MAX || !draw_options_valid(&c->options)) {
    q->rejected++;
    return false;
  }
  q->commands[q->count++] = *c;
  return true;
}

/** @param name const char* recipe. @param o const NgDrawOptions* optional pose. @return bool queued. */
bool ng_draw_shape(const char *name, const NgDrawOptions *o) {
  if (!name || !name[0]) return false;
  NgDrawCommand c = {.kind = NG_DRAW_SHAPE, .options = o ? *o : ng_draw_options(), .graph_index = -1};
  const NgSceneMaterialDesc *material=ng_material_get(c.options.material);
  if(c.options.material.id && (!material || material->font_src[0])) return false;
  bool physics = strncmp(name, "shape:", 6) == 0;
  const NgSceneModelDesc *model = physics ? NULL : mod_scene_assets_get_model(name);
  if (model) {
    if (model->draw != NG_SCENE_DRAW_MESH) return false;
    if(!c.options.material.id) c.options.material=ng_material_lookup(name);
    if(ng_material_get(c.options.material) && ng_material_get(c.options.material)->font_src[0]) return false;
    memcpy(c.model, model->name, sizeof(c.model));
    return draw_submit(&c);
  }
  if (physics) name += 6;
  const ModScenePhysicsCtx *p = mod_scene_runtime_physics();
  for (int i = 0; i < p->shape_count; i++) {
    const NgScenePhysShapeDesc *s = &p->shapes[i];
    if (!s->alive || strcmp(s->name, name)) continue;
    bool sphere = s->type == NG_PHYS_SHAPE_SPHERE;
    strcpy(c.model, sphere ? "@draw_sphere" : "@draw_box");
    const float dimensions[3] = {sphere ? s->radius : 2*s->hx,
      sphere ? s->radius : 2*s->hy, sphere ? s->radius : 2*s->hz};
    for (int j = 0; j < 3; j++) c.options.scale[j] *= dimensions[j];
    return draw_submit(&c);
  }
  return false;
}

/** @param font const char* font recipe. @param text const char* copied UTF-8. @param o const NgDrawOptions* optional style. @return bool queued. */
bool ng_draw_label(const char *font, const char *text, const NgDrawOptions *o) {
  const NgSceneModelDesc *m = font ? mod_scene_assets_get_model(font) : NULL;
  if (!m || m->draw != NG_SCENE_DRAW_MSDF || !text || strlen(text) >= NG_DRAW_TEXT_MAX) return false;
  NgDrawCommand c = {.kind = NG_DRAW_LABEL, .options = o ? *o : ng_draw_options(), .graph_index = -1};
  c.options.material=ng_material_lookup(font);
  memcpy(c.model, m->name, sizeof(c.model));
  strcpy(c.text, text);
  return draw_submit(&c);
}

/** @param now double interpolation clock. @return void; physics and persistent visuals use the public submit path. */
void ng_draw_collect_graph(double now) {
  mod_scene_graph_expire_live_draw(now);
  NgDrawQueue *q = &mod_scene_runtime_active()->draw;
  const ModScenePhysicsCtx *p = mod_scene_runtime_physics();
  const int n = mod_scene_graph_inst_count();
  for (int i = 0; i < n; i++) {
    const NgSceneInst *inst = mod_scene_graph_inst_at(i);
    if (!inst) continue;
    NgDrawOptions o = ng_draw_options();
    o.scope_id = inst->scope_id;
    o.scale[0] = o.scale[1] = o.scale[2] = inst->scale > 0 ? inst->scale : 1;
    if (!mod_scene_graph_sample_draw_pose(inst, now, mod_scene_graph_interp_delay_s(), o.position, o.rotation)) {
      memcpy(o.position, inst->pos, sizeof(o.position));
      memcpy(o.rotation, inst->rot, sizeof(o.rotation));
    }
    const NgSceneModelDesc *m = mod_scene_assets_get_model(inst->model);
    int before = q->count;
    if (m && m->draw == NG_SCENE_DRAW_MSDF) {
      o.size = inst->text_size > 0 ? inst->text_size : 16;
      o.outline = inst->text_outline;
      o.tint[0] = inst->tint_r; o.tint[1] = inst->tint_g; o.tint[2] = inst->tint_b;
      ng_draw_label(inst->model, inst->text, &o);
    } else if (inst->model[0]) {
      ng_draw_shape(inst->model, &o);
    } else if (inst->body[0]) {
      for (int j = 0; j < p->body_count; j++) {
        if (!p->bodies[j].alive || strcmp(p->bodies[j].name, inst->body)) continue;
        char name[40];
        snprintf(name, sizeof(name), "shape:%s", p->bodies[j].shape);
        ng_draw_shape(name, &o);
        break;
      }
    }
    if (q->count > before && m) q->commands[before].graph_index = i;
  }
}
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 21e0c6
// agent: gpt-6-astra | 2026-09-05 | implement shared frame draw queue | 583979
