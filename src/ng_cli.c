// agent: composer-2.5 | 2026-07-25 | C command dispatch table | 1f7c3b
#include "ng_cli.h"
#include "ng_scene.h"
#include <raylib.h>
#include <stdio.h>
#include <string.h>

typedef void (*NgCliHandler)(int argc, const char **argv);

typedef struct NgCliEntry {
  const char *verb;
  NgCliHandler handler;
} NgCliEntry;

static char g_feedback[256];

static void cli_scene(int argc, const char **argv) {
  if (argc < 2) {
    snprintf(g_feedback, sizeof(g_feedback), "usage: scene <id>");
    return;
  }
  if (ng_scene_load(argv[1])) {
    snprintf(g_feedback, sizeof(g_feedback), "scene loaded: %s", argv[1]);
  } else {
    snprintf(g_feedback, sizeof(g_feedback), "scene load failed: %s", argv[1]);
  }
}

static const NgCliEntry g_cmds[] = {
    {"scene", cli_scene},
};

void ng_cli_dispatch(int argc, const char **argv) {
  (void)ng_cli_feedback(argc, argv);
}

const char *ng_cli_feedback(int argc, const char **argv) {
  g_feedback[0] = '\0';

  if (argc <= 0 || !argv[0]) {
    return g_feedback;
  }

  const size_t count = sizeof(g_cmds) / sizeof(g_cmds[0]);
  for (size_t i = 0; i < count; i++) {
    if (strcmp(argv[0], g_cmds[i].verb) == 0) {
      g_cmds[i].handler(argc, argv);
      return g_feedback;
    }
  }

  snprintf(g_feedback, sizeof(g_feedback), "unknown command: %s", argv[0]);
  return g_feedback;
}

const char *ng_cli_last_output(void) { return g_feedback; }
