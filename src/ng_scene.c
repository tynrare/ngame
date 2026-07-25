// agent: composer-2.5 | 2026-07-25 | scoped scene registry | 5a3c8e
#include "ng_scene.h"
#include <raylib.h>
#include <string.h>

#define NG_SCENE_MAX 8

typedef struct NgSceneSlot {
  const NgSceneOps *ops;
  NgScene *instance;
} NgSceneSlot;

static NgSceneSlot g_registry[NG_SCENE_MAX];
static int g_registry_count = 0;
static NgScene *g_active = NULL;

bool ng_scene_register(const NgSceneOps *ops) {
  if (!ops || !ops->id || g_registry_count >= NG_SCENE_MAX) {
    return false;
  }
  for (int i = 0; i < g_registry_count; i++) {
    if (strcmp(g_registry[i].ops->id, ops->id) == 0) {
      return false;
    }
  }
  g_registry[g_registry_count].ops = ops;
  g_registry[g_registry_count].instance = NULL;
  g_registry_count++;
  return true;
}

static NgSceneSlot *ng_scene_find_slot(const char *id) {
  for (int i = 0; i < g_registry_count; i++) {
    if (strcmp(g_registry[i].ops->id, id) == 0) {
      return &g_registry[i];
    }
  }
  return NULL;
}

bool ng_scene_load(const char *id) {
  NgSceneSlot *slot = ng_scene_find_slot(id);
  if (!slot) {
    TraceLog(LOG_WARNING, "NG: unknown scene: %s", id);
    return false;
  }

  if (g_active && g_active->ops && g_active->ops->exit) {
    g_active->ops->exit(g_active);
  }

  if (!slot->instance) {
    slot->instance = slot->ops->create();
    if (!slot->instance) {
      TraceLog(LOG_ERROR, "NG: failed to create scene: %s", id);
      g_active = NULL;
      return false;
    }
  }

  g_active = slot->instance;
  if (g_active->ops && g_active->ops->enter) {
    g_active->ops->enter(g_active);
  }

  TraceLog(LOG_INFO, "NG: scene loaded: %s", id);
  return true;
}

void ng_scene_update(float dt) {
  if (g_active && g_active->ops && g_active->ops->update) {
    g_active->ops->update(g_active, dt);
  }
}

void ng_scene_draw(void) {
  if (g_active && g_active->ops && g_active->ops->draw) {
    g_active->ops->draw(g_active);
  }
}

void ng_scene_shutdown(void) {
  if (g_active && g_active->ops && g_active->ops->exit) {
    g_active->ops->exit(g_active);
  }
  g_active = NULL;

  for (int i = 0; i < g_registry_count; i++) {
    if (g_registry[i].instance && g_registry[i].ops->destroy) {
      g_registry[i].ops->destroy(g_registry[i].instance);
      g_registry[i].instance = NULL;
    }
  }
}

const char *ng_scene_active_id(void) {
  if (!g_active || !g_active->ops) {
    return NULL;
  }
  return g_active->ops->id;
}
