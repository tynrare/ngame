// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 6ffedb
/* Flow ID: material-recipe (canonical owner).
 * 1) describe/lookup → copy shader/texture/state recipe → process-unique typed ID.
 * 2) draw submission → validate ID in active runtime → immutable frame command.
 * 3) renderer → cache GPU resources by ID → assignments reuse loaded resources.
 * 4) assets reset → erase registry → stale handles rejected, including across runtimes.
 * Invariants: zero means omitted; IDs are never reused; exhaustion rejects registration.
 */
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

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 6ffedb
/** @param name const char* model. @param mesh const char* geometry. @param shader const char* shader. @param albedo const char* texture. @return bool registered with default material. */
bool mod_scene_assets_describe_model(const char *name, const char *mesh, const char *shader,
                                     const char *albedo) {
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
    // agent: grok-4.6 | 2026-08-31 | copy albedo into resolved | a8f4ad
    existing->albedo[0] = '\0';
    if (albedo && albedo[0]) {
      mod_scene_assets_normalize_res_path(existing->albedo, sizeof(existing->albedo), albedo);
    }
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 6ffedb
    ng_material_lookup(name);
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
  if (albedo && albedo[0]) {
    mod_scene_assets_normalize_res_path(m->albedo, sizeof(m->albedo), albedo);
  }
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 6ffedb
  ng_material_lookup(name);
  return true;
}

// agent: grok-4.6 | 2026-08-30 | describe font without mesh | 3b5e0c
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 6ffedb
/** @param name const char* font. @param src const char* TTF. @return bool registered with automatic MSDF material. */
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
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 6ffedb
    ng_material_lookup(name);
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
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 6ffedb
  ng_material_lookup(name);
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

// agent: grok-4.6 | 2026-08-30 | named scope table APIs | 6605bc
static NgSceneScopeDesc *mod_scene_assets_find_scope(const char *name) {
  if (!name) {
    return NULL;
  }
  for (int i = 0; i < GASSETS().scope_count; i++) {
    NgSceneScopeDesc *s = &GASSETS().scopes[i];
    if (s->alive && strcmp(s->name, name) == 0) {
      return s;
    }
  }
  return NULL;
}

static void mod_scene_assets_sync_view(void) {
  for (int i = 0; i < GASSETS().scene_scope_count; i++) {
    const int id = GASSETS().scene_scope_ids[i];
    if (id < 0 || id >= GASSETS().scope_count) {
      continue;
    }
    NgSceneScopeDesc *s = &GASSETS().scopes[id];
    if (s->alive && s->meta.camera_mode != NG_SCENE_CAM_ORTHO) {
      GASSETS().view = s->meta;
      GASSETS().view.valid = true;
      return;
    }
  }
}

static int mod_scene_assets_scope_index(const NgSceneScopeDesc *s) {
  if (!s) {
    return -1;
  }
  return (int)(s - GASSETS().scopes);
}

bool mod_scene_assets_describe_view(const NgSceneViewMeta *view) {
  if (!view) {
    return false;
  }
  GASSETS().view = *view;
  GASSETS().view.valid = true;
  GASSETS().legacy_scopes = true;
  NgSceneScopeDesc *s = mod_scene_assets_find_scope("");
  if (!s) {
    if (GASSETS().scope_count >= NG_SCENE_ASSET_MAX) {
      return true;
    }
    s = &GASSETS().scopes[GASSETS().scope_count++];
    memset(s, 0, sizeof(*s));
    s->alive = true;
  }
  s->meta = *view;
  s->meta.valid = true;
  GASSETS().scene_scope_ids[0] = (uint8_t)mod_scene_assets_scope_index(s);
  GASSETS().scene_scope_count = 1;
  return true;
}

bool mod_scene_assets_describe_scope(const char *name, const NgSceneViewMeta *view) {
  if (!name || !name[0] || !view) {
    return false;
  }
  NgSceneScopeDesc *s = mod_scene_assets_find_scope(name);
  if (!s) {
    if (GASSETS().scope_count >= NG_SCENE_ASSET_MAX) {
      return false;
    }
    s = &GASSETS().scopes[GASSETS().scope_count++];
    memset(s, 0, sizeof(*s));
    s->alive = true;
    strncpy(s->name, name, sizeof(s->name) - 1);
  }
  s->meta = *view;
  s->meta.valid = true;
  return true;
}

bool mod_scene_assets_bind_scene_scopes(const char *const *names, int n) {
  GASSETS().legacy_scopes = false;
  GASSETS().scene_scope_count = 0;
  if (!names || n <= 0) {
    return false;
  }
  for (int i = 0; i < n && GASSETS().scene_scope_count < NG_SCENE_ASSET_MAX; i++) {
    const int id = mod_scene_assets_lookup_scope(names[i]);
    if (id < 0) {
      continue;
    }
    GASSETS().scene_scope_ids[GASSETS().scene_scope_count++] = (uint8_t)id;
  }
  mod_scene_assets_sync_view();
  return GASSETS().scene_scope_count > 0;
}

bool mod_scene_assets_legacy_scopes(void) {
  return GASSETS().legacy_scopes;
}

int mod_scene_assets_lookup_scope(const char *name) {
  return mod_scene_assets_scope_index(mod_scene_assets_find_scope(name));
}

int mod_scene_assets_default_scope_id(void) {
  if (GASSETS().scene_scope_count > 0) {
    return GASSETS().scene_scope_ids[0];
  }
  return 0;
}

int mod_scene_assets_world_scope_id(void) {
  for (int i = 0; i < GASSETS().scene_scope_count; i++) {
    const int id = GASSETS().scene_scope_ids[i];
    const NgSceneScopeDesc *s = mod_scene_assets_scope_at(id);
    if (s && s->alive && s->meta.camera_mode != NG_SCENE_CAM_ORTHO) {
      return id;
    }
  }
  return mod_scene_assets_default_scope_id();
}

bool mod_scene_assets_ortho_scope_id(uint8_t *out_id) {
  if (GASSETS().legacy_scopes) {
    return false;
  }
  for (int i = 0; i < GASSETS().scene_scope_count; i++) {
    const int id = GASSETS().scene_scope_ids[i];
    const NgSceneScopeDesc *s = mod_scene_assets_scope_at(id);
    if (s && s->alive && s->meta.camera_mode == NG_SCENE_CAM_ORTHO) {
      if (out_id) {
        *out_id = (uint8_t)id;
      }
      return true;
    }
  }
  return false;
}

const NgSceneScopeDesc *mod_scene_assets_scope_at(int id) {
  if (id < 0 || id >= GASSETS().scope_count) {
    return NULL;
  }
  return &GASSETS().scopes[id];
}

// agent: composer-2.5 | 2026-08-09 | set view camera assets | d004a6
bool mod_scene_assets_set_view_camera(const float *pos, const float *target) {
  NgSceneViewMeta *meta = NULL;
  for (int i = 0; i < GASSETS().scene_scope_count; i++) {
    const int id = GASSETS().scene_scope_ids[i];
    NgSceneScopeDesc *s = (NgSceneScopeDesc *)mod_scene_assets_scope_at(id);
    if (s && s->alive && s->meta.camera_mode != NG_SCENE_CAM_ORTHO) {
      meta = &s->meta;
      break;
    }
  }
  if (!meta) {
    if (!GASSETS().view.valid) {
      return false;
    }
    meta = &GASSETS().view;
  }
  if (pos) {
    meta->cam_pos[0] = pos[0];
    meta->cam_pos[1] = pos[1];
    meta->cam_pos[2] = pos[2];
  }
  if (target) {
    meta->cam_target[0] = target[0];
    meta->cam_target[1] = target[1];
    meta->cam_target[2] = target[2];
  }
  meta->camera_mode = NG_SCENE_CAM_FIXED;
  GASSETS().view = *meta;
  GASSETS().view.valid = true;
  return true;
}

const NgSceneViewMeta *mod_scene_assets_view(void) {
  return GASSETS().view.valid ? &GASSETS().view : NULL;
}

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 6ffedb
/** @param kind const char* recipe kind. @param name const char* name. @return bool disposed, invalidating related material handles. */
bool mod_scene_assets_dispose(const char *kind, const char *name) {
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 6ffedb
  if(!strcmp(kind,"material") || !strcmp(kind,"model") || !strcmp(kind,"font")) {
    for(int i=0;i<GASSETS().material_count;i++) if(!strcmp(name,GASSETS().materials[i].name)) {
      GASSETS().materials[i].handle.id=0; GASSETS().materials[i].name[0]=0;
      if(!strcmp(kind,"material")) return true;
    }
  }

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
  } else if (strcmp(kind, "scope") == 0) {
    // agent: grok-4.6 | 2026-08-30 | named scope table APIs | 6605bc
    NgSceneScopeDesc *s = mod_scene_assets_find_scope(name);
    if (s) {
      s->alive = false;
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
  // agent: grok-4.6 | 2026-08-31 | copy albedo into resolved | a8f4ad
  out->albedo[0] = '\0';
  if (model->albedo[0]) {
    strncpy(out->albedo, model->albedo, sizeof(out->albedo) - 1);
  }
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

// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 6ffedb
static uint32_t material_serial;
/** @param handle NgMaterialHandle candidate. @return const NgSceneMaterialDesc* active recipe or NULL. */
const NgSceneMaterialDesc *ng_material_get(NgMaterialHandle handle) {
  if(!handle.id) return NULL;
  for(int i=0;i<GASSETS().material_count;i++)
    if(GASSETS().materials[i].handle.id==handle.id) return &GASSETS().materials[i];
  return NULL;
}
/** @param name const char* name. @param recipe const NgSceneResolvedModel* values. @return NgSceneMaterialDesc* new stable slot or NULL. */
static NgSceneMaterialDesc *material_add(const char *name, const NgSceneResolvedModel *recipe) {
  if(!name || !name[0] || strlen(name)>=32 || GASSETS().material_count>=NG_SCENE_ASSET_MAX*2 || material_serial==UINT32_MAX) return NULL;
  NgSceneMaterialDesc *m=&GASSETS().materials[GASSETS().material_count++];
  *m=(NgSceneMaterialDesc){.handle={++material_serial},.recipe=*recipe,.depth_test=true,.depth_write=true};
  strcpy(m->name,name); return m;
}
/** @param name const char* recipe/default model. @return NgMaterialHandle resolved once. */
NgMaterialHandle ng_material_lookup(const char *name) {
  if(!name) return (NgMaterialHandle){0};
  for(int i=0;i<GASSETS().material_count;i++)
    if(!strcmp(name,GASSETS().materials[i].name)) return GASSETS().materials[i].handle;
  const NgSceneModelDesc *model=mod_scene_assets_get_model(name);
  NgSceneResolvedModel recipe={0};
  if(!model || (model->draw!=NG_SCENE_DRAW_MSDF && !mod_scene_assets_resolve_model(name,&recipe))) return (NgMaterialHandle){0};
  NgSceneMaterialDesc *m=material_add(name,&recipe); if(!m) return (NgMaterialHandle){0};
  if(model->draw==NG_SCENE_DRAW_MSDF) {
    strcpy(m->font_src,model->font_src); strcpy(m->recipe.vertex,"shaders/msdf_inst.vs");
    strcpy(m->recipe.fragment,"shaders/msdf_font.fs"); m->blend=true; m->depth_write=false;
  }
  return m->handle;
}
/** @param name const char* name. @param shader const char* recipe. @param albedo const char* texture. @param blend bool blend. @param depth_test bool test. @param depth_write bool write. @return bool registered. */
bool ng_material_describe(const char *name,const char *shader,const char *albedo,bool blend,bool depth_test,bool depth_write) {
  if(!name || !name[0]) return false;
  const NgSceneShaderDesc *s=mod_scene_assets_find_shader(shader); if(!s) return false;
  for(int i=0;i<GASSETS().material_count;i++) if(!strcmp(name,GASSETS().materials[i].name)) return false;
  NgSceneResolvedModel r={.ok=true,.have_tint=s->have_tint,.tint_r=s->tint_r,.tint_g=s->tint_g,.tint_b=s->tint_b,
    .have_glow=s->have_glow,.glow_r=s->glow_r,.glow_g=s->glow_g,.glow_b=s->glow_b,.roughness=s->roughness,.metalness=s->metalness};
  strcpy(r.vertex,s->vertex); strcpy(r.fragment,s->fragment);
  if(albedo) snprintf(r.albedo,sizeof(r.albedo),"%s",albedo);
  NgSceneMaterialDesc *m=material_add(name,&r); if(!m) return false;
  m->blend=blend; m->depth_test=depth_test; m->depth_write=depth_write; return true;
}
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 6ffedb

// agent: composer-2.5 | 2026-07-28 | js-driven scene asset registry | c1d2e3
// agent: composer-2.5 | 2026-07-28 | parse mesh shape from js field | a4b5c6
// agent: composer-2.5 | 2026-08-09 | shader glow rough metal uniforms | c86521
// agent: composer-2.5 | 2026-08-09 | set view camera assets | d004a6
// agent: grok-4.6 | 2026-08-30 | describe font without mesh | 3b5e0c
// agent: grok-4.6 | 2026-08-30 | named scope table APIs | 6605bc
// agent: grok-4.6 | 2026-08-31 | copy albedo into resolved | a8f4ad
