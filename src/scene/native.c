// agent: composer-2.5 | 2026-07-28 | optional native scene plugins | d7e8f9
#include "native.h"
#include "assets.h"
#include <string.h>

typedef struct NgNativeSceneOps {
  const char *id;
  bool (*load)(void);
  void (*unload)(void);
  void (*step)(float dt);
  const char *(*banner)(void);
} NgNativeSceneOps;

static const NgNativeSceneOps *g_active_native = NULL;

static bool dtest_load(void) {
  NgSceneViewMeta view = {
      .valid = true,
      .bg_r = 16,
      .bg_g = 16,
      .bg_b = 24,
      .camera_mode = NG_SCENE_CAM_FIXED,
      .cam_pos = {0.0f, 2.0f, 8.0f},
      .cam_target = {0.0f, 0.0f, 0.0f},
      .cam_fovy = 45.0f,
  };
  mod_scene_assets_describe_view(&view);
  return true;
}

static void dtest_unload(void) { (void)0; }

static void dtest_step(float dt) { (void)dt; }

static const char *dtest_banner(void) { return "this is C custom scene code"; }

static const NgNativeSceneOps g_native_dtest = {
    .id = "d-test",
    .load = dtest_load,
    .unload = dtest_unload,
    .step = dtest_step,
    .banner = dtest_banner,
};

static const NgNativeSceneOps *g_native_table[] = {
    &g_native_dtest,
};

bool mod_scene_native_load(const char *scene_id) {
  if (!scene_id) {
    return false;
  }
  for (size_t i = 0; i < sizeof(g_native_table) / sizeof(g_native_table[0]); i++) {
    const NgNativeSceneOps *ops = g_native_table[i];
    if (ops && ops->id && strcmp(ops->id, scene_id) == 0 && ops->load && ops->load()) {
      g_active_native = ops;
      return true;
    }
  }
  return false;
}

void mod_scene_native_unload(void) {
  if (g_active_native && g_active_native->unload) {
    g_active_native->unload();
  }
  g_active_native = NULL;
}

void mod_scene_native_step(float dt) {
  if (g_active_native && g_active_native->step) {
    g_active_native->step(dt);
  }
}

const char *mod_scene_native_banner(void) {
  if (g_active_native && g_active_native->banner) {
    return g_active_native->banner();
  }
  return NULL;
}

bool mod_scene_native_active(void) { return g_active_native != NULL; }

// agent: composer-2.5 | 2026-07-28 | optional native scene plugins | d7e8f9
