// agent: composer-2.5 | 2026-07-28 | js-driven scene asset registry | c1d2e3
// agent: composer-2.5 | 2026-07-29 | assets use active runtime | ce9266
// agent: composer-2.5 | 2026-08-09 | shader glow rough metal uniforms | c86521
// agent: composer-2.5 | 2026-08-09 | set view camera assets | d004a6
#include "assets.h"
#include "scene/runtime.h"
#include <stdio.h>
#include <string.h>

#define GASSETS() (*mod_scene_runtime_assets())

static NgSceneMeshDesc *mod_scene_assets_find_mesh(const char *name) {
  if (!name) {
    return NULL;
  }
  for (int i = 0; i < GASSETS().mesh_count; i++) {
    NgSceneMeshDesc *m = &GASSETS().meshes[i];
    if (m->alive && strcmp(m->name, name) == 0) {
      return m;
    }
  }
  return NULL;
}

static NgSceneShaderDesc *mod_scene_assets_find_shader(const char *name) {
  if (!name) {
    return NULL;
  }
  for (int i = 0; i < GASSETS().shader_count; i++) {
    NgSceneShaderDesc *s = &GASSETS().shaders[i];
    if (s->alive && strcmp(s->name, name) == 0) {
      return s;
    }
  }
  return NULL;
}

static NgSceneModelDesc *mod_scene_assets_find_model(const char *name) {
  if (!name) {
    return NULL;
  }
  for (int i = 0; i < GASSETS().model_count; i++) {
    NgSceneModelDesc *m = &GASSETS().models[i];
    if (m->alive && strcmp(m->name, name) == 0) {
      return m;
    }
  }
  return NULL;
}

// agent: composer-2.5 | 2026-07-28 | normalize shader paths under resroot | 8108b6
static void mod_scene_assets_normalize_res_path(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) {
    return;
  }
  dst[0] = '\0';
  if (!src || src[0] == '\0') {
    return;
  }
  if (strchr(src, '/') != NULL) {
    strncpy(dst, src, cap - 1);
  } else {
    snprintf(dst, cap, "shaders/%s", src);
  }
  dst[cap - 1] = '\0';
}

// agent: composer-2.5 | 2026-07-28 | parse mesh shape from js field | a4b5c6
static NgSceneMeshKind mod_scene_assets_parse_shape(const char *shape) {
  if (shape && strcmp(shape, "sphere") == 0) {
    return NG_SCENE_MESH_SPHERE;
  }
  return NG_SCENE_MESH_CUBE;
}

NgEntityType mod_scene_assets_entity_type_for_kind(NgSceneMeshKind kind) {
  return kind == NG_SCENE_MESH_SPHERE ? NG_ENTITY_SPHERE : NG_ENTITY_CUBE;
}

void mod_scene_assets_reset(void) {
  ModSceneAssetsCtx *a = mod_scene_runtime_assets();
  memset(a, 0, sizeof(*a));
}

bool mod_scene_assets_describe_mesh(const char *name, const char *shape, float w, float h,
                                    float d) {
  if (!name) {
    return false;
  }
  const NgSceneMeshKind kind = mod_scene_assets_parse_shape(shape);
  NgSceneMeshDesc *existing = mod_scene_assets_find_mesh(name);
  if (existing) {
    existing->kind = kind;
    existing->width = w > 0.0f ? w : 1.0f;
    existing->height = h > 0.0f ? h : 1.0f;
    existing->depth = d > 0.0f ? d : 1.0f;
    return true;
  }
  if (GASSETS().mesh_count >= NG_SCENE_ASSET_MAX) {
    return false;
  }
  NgSceneMeshDesc *m = &GASSETS().meshes[GASSETS().mesh_count++];
  memset(m, 0, sizeof(*m));
  m->alive = true;
  strncpy(m->name, name, sizeof(m->name) - 1);
  m->kind = kind;
  m->width = w > 0.0f ? w : 1.0f;
  m->height = h > 0.0f ? h : 1.0f;
  m->depth = d > 0.0f ? d : 1.0f;
  return true;
}

bool mod_scene_assets_describe_shader(const char *name, const char *fragment, const char *vertex,
                                      uint8_t tint_r, uint8_t tint_g, uint8_t tint_b,
                                      bool have_tint, uint8_t glow_r, uint8_t glow_g,
                                      uint8_t glow_b, bool have_glow, float roughness,
                                      float metalness) {
  if (!name || !fragment) {
    return false;
  }
  NgSceneShaderDesc *existing = mod_scene_assets_find_shader(name);
  if (existing) {
    mod_scene_assets_normalize_res_path(existing->fragment, sizeof(existing->fragment), fragment);
    if (vertex) {
      mod_scene_assets_normalize_res_path(existing->vertex, sizeof(existing->vertex), vertex);
    }
    existing->have_tint = have_tint;
    existing->tint_r = tint_r;
    existing->tint_g = tint_g;
    existing->tint_b = tint_b;
    existing->have_glow = have_glow;
    existing->glow_r = glow_r;
    existing->glow_g = glow_g;
    existing->glow_b = glow_b;
    existing->roughness = roughness;
    existing->metalness = metalness;
    return true;
  }
  if (GASSETS().shader_count >= NG_SCENE_ASSET_MAX) {
    return false;
  }
  NgSceneShaderDesc *s = &GASSETS().shaders[GASSETS().shader_count++];
  memset(s, 0, sizeof(*s));
  s->alive = true;
  strncpy(s->name, name, sizeof(s->name) - 1);
  mod_scene_assets_normalize_res_path(s->fragment, sizeof(s->fragment), fragment);
  if (vertex) {
    mod_scene_assets_normalize_res_path(s->vertex, sizeof(s->vertex), vertex);
  } else {
    mod_scene_assets_normalize_res_path(s->vertex, sizeof(s->vertex), "mesh.vs");
  }
  s->have_tint = have_tint;
  s->tint_r = tint_r;
  s->tint_g = tint_g;
  s->tint_b = tint_b;
  s->have_glow = have_glow;
  s->glow_r = glow_r;
  s->glow_g = glow_g;
  s->glow_b = glow_b;
  s->roughness = roughness;
  s->metalness = metalness;
  return true;
}

bool mod_scene_assets_describe_model(const char *name, const char *mesh, const char *shader) {
  if (!name || !mesh) {
    return false;
  }
  const NgSceneMeshDesc *md = mod_scene_assets_find_mesh(mesh);
  if (!md) {
    return false;
  }
  NgSceneModelDesc *existing = mod_scene_assets_find_model(name);
  if (existing) {
    strncpy(existing->mesh, mesh, sizeof(existing->mesh) - 1);
    if (shader) {
      strncpy(existing->shader, shader, sizeof(existing->shader) - 1);
    }
    existing->mesh_kind = md->kind;
    existing->draw = NG_SCENE_DRAW_MESH;
    existing->font_src[0] = '\0';
    return true;
  }
  if (GASSETS().model_count >= NG_SCENE_ASSET_MAX) {
    return false;
  }
  NgSceneModelDesc *m = &GASSETS().models[GASSETS().model_count++];
  memset(m, 0, sizeof(*m));
  m->alive = true;
  strncpy(m->name, name, sizeof(m->name) - 1);
  strncpy(m->mesh, mesh, sizeof(m->mesh) - 1);
  if (shader) {
    strncpy(m->shader, shader, sizeof(m->shader) - 1);
  }
  m->mesh_kind = md->kind;
  m->draw = NG_SCENE_DRAW_MESH;
  return true;
}

// agent: grok-4.6 | 2026-08-30 | describe font without mesh | 3b5e0c
bool mod_scene_assets_describe_font(const char *name, const char *src) {
  if (!name) {
    return false;
  }
  if (!src || src[0] == '\0') {
    src = "fonts/LiberationSans-Regular.ttf";
  }
  NgSceneModelDesc *existing = mod_scene_assets_find_model(name);
  if (existing) {
    existing->draw = NG_SCENE_DRAW_MSDF;
    existing->mesh[0] = '\0';
    existing->shader[0] = '\0';
    strncpy(existing->font_src, src, sizeof(existing->font_src) - 1);
    return true;
  }
  if (GASSETS().model_count >= NG_SCENE_ASSET_MAX) {
    return false;
  }
  NgSceneModelDesc *m = &GASSETS().models[GASSETS().model_count++];
  memset(m, 0, sizeof(*m));
  m->alive = true;
  strncpy(m->name, name, sizeof(m->name) - 1);
  m->draw = NG_SCENE_DRAW_MSDF;
  strncpy(m->font_src, src, sizeof(m->font_src) - 1);
  return true;
}

const NgSceneModelDesc *mod_scene_assets_get_model(const char *name) {
  return mod_scene_assets_find_model(name);
}

const char *mod_scene_assets_first_font_src(void) {
  for (int i = 0; i < GASSETS().model_count; i++) {
    const NgSceneModelDesc *m = &GASSETS().models[i];
    if (m->alive && m->draw == NG_SCENE_DRAW_MSDF && m->font_src[0]) {
      return m->font_src;
    }
  }
  return NULL;
}

bool mod_scene_assets_describe_view(const NgSceneViewMeta *view) {
  if (!view) {
    return false;
  }
  GASSETS().view = *view;
  GASSETS().view.valid = true;
  return true;
}

// agent: composer-2.5 | 2026-08-09 | set view camera assets | d004a6
bool mod_scene_assets_set_view_camera(const float *pos, const float *target) {
  if (!GASSETS().view.valid) {
    return false;
  }
  if (pos) {
    GASSETS().view.cam_pos[0] = pos[0];
    GASSETS().view.cam_pos[1] = pos[1];
    GASSETS().view.cam_pos[2] = pos[2];
  }
  if (target) {
    GASSETS().view.cam_target[0] = target[0];
    GASSETS().view.cam_target[1] = target[1];
    GASSETS().view.cam_target[2] = target[2];
  }
  GASSETS().view.camera_mode = NG_SCENE_CAM_FIXED;
  return true;
}

const NgSceneViewMeta *mod_scene_assets_view(void) {
  return GASSETS().view.valid ? &GASSETS().view : NULL;
}

bool mod_scene_assets_dispose(const char *kind, const char *name) {
  if (!kind || !name) {
    return false;
  }
  if (strcmp(kind, "mesh") == 0) {
    NgSceneMeshDesc *m = mod_scene_assets_find_mesh(name);
    if (m) {
      m->alive = false;
      return true;
    }
  } else if (strcmp(kind, "shader") == 0) {
    NgSceneShaderDesc *s = mod_scene_assets_find_shader(name);
    if (s) {
      s->alive = false;
      return true;
    }
  } else if (strcmp(kind, "model") == 0) {
    NgSceneModelDesc *m = mod_scene_assets_find_model(name);
    if (m) {
      m->alive = false;
      return true;
    }
  }
  return false;
}

static bool mod_scene_assets_fill_resolved(const NgSceneModelDesc *model, NgSceneResolvedModel *out) {
  if (!model || model->draw == NG_SCENE_DRAW_MSDF) {
    return false;
  }
  const NgSceneMeshDesc *mesh = mod_scene_assets_find_mesh(model->mesh);
  const NgSceneShaderDesc *shader =
      model->shader[0] != '\0' ? mod_scene_assets_find_shader(model->shader) : NULL;
  if (!mesh || !shader) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  out->ok = true;
  out->mesh_kind = mesh->kind;
  out->mesh_w = mesh->width;
  out->mesh_h = mesh->height;
  out->mesh_d = mesh->depth;
  mod_scene_assets_normalize_res_path(out->fragment, sizeof(out->fragment), shader->fragment);
  mod_scene_assets_normalize_res_path(out->vertex, sizeof(out->vertex), shader->vertex);
  out->have_tint = shader->have_tint;
  out->tint_r = shader->tint_r;
  out->tint_g = shader->tint_g;
  out->tint_b = shader->tint_b;
  out->have_glow = shader->have_glow;
  out->glow_r = shader->glow_r;
  out->glow_g = shader->glow_g;
  out->glow_b = shader->glow_b;
  out->roughness = shader->roughness;
  out->metalness = shader->metalness;
  return true;
}

bool mod_scene_assets_resolve_model(const char *model_name, NgSceneResolvedModel *out) {
  if (!model_name || !out) {
    return false;
  }
  const NgSceneModelDesc *model = mod_scene_assets_find_model(model_name);
  if (!model) {
    return false;
  }
  return mod_scene_assets_fill_resolved(model, out);
}

bool mod_scene_assets_resolve_model_for_mesh_kind(NgSceneMeshKind kind, NgSceneResolvedModel *out) {
  if (!out) {
    return false;
  }
  for (int i = 0; i < GASSETS().model_count; i++) {
    NgSceneModelDesc *model = &GASSETS().models[i];
    if (!model->alive || model->draw == NG_SCENE_DRAW_MSDF || model->mesh_kind != kind) {
      continue;
    }
    return mod_scene_assets_fill_resolved(model, out);
  }
  return false;
}

// agent: composer-2.5 | 2026-07-28 | js-driven scene asset registry | c1d2e3
// agent: composer-2.5 | 2026-07-28 | parse mesh shape from js field | a4b5c6
// agent: composer-2.5 | 2026-08-09 | shader glow rough metal uniforms | c86521
// agent: composer-2.5 | 2026-08-09 | set view camera assets | d004a6
// agent: grok-4.6 | 2026-08-30 | describe font without mesh | 3b5e0c
