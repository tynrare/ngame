// agent: composer-2.5 | 2026-07-28 | js-driven scene asset registry | c1d2e3
#include "assets.h"
#include <stdio.h>
#include <string.h>

typedef struct ModSceneAssetsCtx {
  NgSceneMeshDesc meshes[NG_SCENE_ASSET_MAX];
  int mesh_count;
  NgSceneShaderDesc shaders[NG_SCENE_ASSET_MAX];
  int shader_count;
  NgSceneModelDesc models[NG_SCENE_ASSET_MAX];
  int model_count;
  NgSceneViewMeta view;
} ModSceneAssetsCtx;

static ModSceneAssetsCtx g_assets;

static NgSceneMeshDesc *mod_scene_assets_find_mesh(const char *name) {
  if (!name) {
    return NULL;
  }
  for (int i = 0; i < g_assets.mesh_count; i++) {
    NgSceneMeshDesc *m = &g_assets.meshes[i];
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
  for (int i = 0; i < g_assets.shader_count; i++) {
    NgSceneShaderDesc *s = &g_assets.shaders[i];
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
  for (int i = 0; i < g_assets.model_count; i++) {
    NgSceneModelDesc *m = &g_assets.models[i];
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
  memset(&g_assets, 0, sizeof(g_assets));
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
  if (g_assets.mesh_count >= NG_SCENE_ASSET_MAX) {
    return false;
  }
  NgSceneMeshDesc *m = &g_assets.meshes[g_assets.mesh_count++];
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
                                      bool have_tint) {
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
    return true;
  }
  if (g_assets.shader_count >= NG_SCENE_ASSET_MAX) {
    return false;
  }
  NgSceneShaderDesc *s = &g_assets.shaders[g_assets.shader_count++];
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
    return true;
  }
  if (g_assets.model_count >= NG_SCENE_ASSET_MAX) {
    return false;
  }
  NgSceneModelDesc *m = &g_assets.models[g_assets.model_count++];
  memset(m, 0, sizeof(*m));
  m->alive = true;
  strncpy(m->name, name, sizeof(m->name) - 1);
  strncpy(m->mesh, mesh, sizeof(m->mesh) - 1);
  if (shader) {
    strncpy(m->shader, shader, sizeof(m->shader) - 1);
  }
  m->mesh_kind = md->kind;
  return true;
}

bool mod_scene_assets_describe_view(const NgSceneViewMeta *view) {
  if (!view) {
    return false;
  }
  g_assets.view = *view;
  g_assets.view.valid = true;
  return true;
}

const NgSceneViewMeta *mod_scene_assets_view(void) {
  return g_assets.view.valid ? &g_assets.view : NULL;
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
  for (int i = 0; i < g_assets.model_count; i++) {
    NgSceneModelDesc *model = &g_assets.models[i];
    if (!model->alive || model->mesh_kind != kind) {
      continue;
    }
    return mod_scene_assets_fill_resolved(model, out);
  }
  return false;
}

// agent: composer-2.5 | 2026-07-28 | js-driven scene asset registry | c1d2e3
// agent: composer-2.5 | 2026-07-28 | parse mesh shape from js field | a4b5c6
