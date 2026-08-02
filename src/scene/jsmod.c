// agent: composer-2.5 | 2026-08-01 | js module catalog path resolve | dea7c1
// agent: composer-2.5 | 2026-08-02 | preserve abs res root slash | b81da8
#include "jsmod.h"
#include "engine/ng_log.h"
#include "ng_path.h"
#include <stdio.h>
#include <string.h>

typedef struct NgJsModEntry {
  char id[NG_JSMOD_ID_MAX];
  char path[NG_JSMOD_PATH_MAX];
} NgJsModEntry;

static NgJsModEntry g_jsmod_catalog[NG_JSMOD_CATALOG_MAX];
static int g_jsmod_catalog_n;
static char g_jsmod_current_file[NG_JSMOD_PATH_MAX];

void ng_jsmod_set_current_file(const char *path) {
  if (!path || path[0] == '\0') {
    g_jsmod_current_file[0] = '\0';
    return;
  }
  strncpy(g_jsmod_current_file, path, sizeof(g_jsmod_current_file) - 1);
  g_jsmod_current_file[sizeof(g_jsmod_current_file) - 1] = '\0';
}

const char *ng_jsmod_current_file(void) {
  return g_jsmod_current_file[0] ? g_jsmod_current_file : NULL;
}

static void ng_jsmod_dirname(const char *path, char *out, size_t out_cap) {
  out[0] = '\0';
  if (!path || !out || out_cap == 0) {
    return;
  }
  const char *slash = strrchr(path, '/');
  if (!slash) {
    strncpy(out, ".", out_cap - 1);
    out[out_cap - 1] = '\0';
    return;
  }
  size_t n = (size_t)(slash - path);
  if (n == 0) {
    strncpy(out, "/", out_cap - 1);
    out[out_cap - 1] = '\0';
    return;
  }
  if (n >= out_cap) {
    n = out_cap - 1;
  }
  memcpy(out, path, n);
  out[n] = '\0';
}

/* Collapse . and ..; require final path under NG_RES_ROOT.
 * Web uses absolute NG_RES_ROOT ("/res/"); rebuild must keep the leading '/'. */
// agent: composer-2.5 | 2026-08-02 | preserve abs res root slash | b81da8
static bool ng_jsmod_normalize(const char *in, char *out, size_t out_cap) {
  if (!in || !out || out_cap < 8) {
    return false;
  }
  char parts[48][64];
  int nparts = 0;
  const char *p = in;
  while (*p) {
    while (*p == '/') {
      p++;
    }
    if (*p == '\0') {
      break;
    }
    const char *start = p;
    while (*p && *p != '/') {
      p++;
    }
    size_t len = (size_t)(p - start);
    if (len == 1 && start[0] == '.') {
      continue;
    }
    if (len == 2 && start[0] == '.' && start[1] == '.') {
      if (nparts > 0) {
        nparts--;
      }
      continue;
    }
    if (nparts >= 48 || len >= 64) {
      return false;
    }
    memcpy(parts[nparts], start, len);
    parts[nparts][len] = '\0';
    nparts++;
  }

  const bool abs_out = (NG_RES_ROOT[0] == '/');
  out[0] = '\0';
  size_t used = 0;
  if (abs_out) {
    out[0] = '/';
    out[1] = '\0';
    used = 1;
  }
  for (int i = 0; i < nparts; i++) {
    if ((abs_out && used > 1) || (!abs_out && used > 0)) {
      if (used + 1 >= out_cap) {
        return false;
      }
      out[used++] = '/';
      out[used] = '\0';
    }
    size_t pl = strlen(parts[i]);
    if (used + pl >= out_cap) {
      return false;
    }
    memcpy(out + used, parts[i], pl + 1);
    used += pl;
  }

  const char *root = NG_RES_ROOT;
  size_t root_len = strlen(root);
  if (root_len > 0 && root[root_len - 1] == '/') {
    root_len--;
  }
  if (strncmp(out, root, root_len) != 0) {
    return false;
  }
  if (out[root_len] != '\0' && out[root_len] != '/') {
    return false;
  }
  return out[0] != '\0' && !(abs_out && used <= 1);
}

bool ng_jsmod_resolve_path(const char *path_in, const char *current_file, char *out, size_t out_cap) {
  if (!path_in || path_in[0] == '\0' || !out || out_cap == 0) {
    return false;
  }
  while (*path_in == '/') {
    path_in++;
  }

  char joined[NG_JSMOD_PATH_MAX];
  const bool relative = (strncmp(path_in, "./", 2) == 0 || strncmp(path_in, "../", 3) == 0);
  if (relative) {
    if (!current_file || current_file[0] == '\0') {
      NG_LOG_ERROR("module: relative path without current file: %s", path_in);
      return false;
    }
    char dir[NG_JSMOD_PATH_MAX];
    ng_jsmod_dirname(current_file, dir, sizeof(dir));
    if (snprintf(joined, sizeof(joined), "%s/%s", dir, path_in) >= (int)sizeof(joined)) {
      return false;
    }
  } else {
    /* Res-root: strip optional "res/" prefix then join NG_RES_ROOT. */
    const char *rel = path_in;
    if (strncmp(rel, "res/", 4) == 0) {
      rel = path_in + 4;
    }
    if (snprintf(joined, sizeof(joined), "%s%s", NG_RES_ROOT, rel) >= (int)sizeof(joined)) {
      return false;
    }
  }

  if (!ng_jsmod_normalize(joined, out, out_cap)) {
    NG_LOG_ERROR("module: path escapes res/ or invalid: %s", path_in);
    return false;
  }
  return true;
}

bool ng_jsmod_register(const char *id, const char *path_in) {
  if (!id || id[0] == '\0' || !path_in || path_in[0] == '\0') {
    NG_LOG_ERROR("module: register empty id or path");
    return false;
  }
  if (strlen(id) >= NG_JSMOD_ID_MAX) {
    NG_LOG_ERROR("module: register id too long: %s", id);
    return false;
  }

  char resolved[NG_JSMOD_PATH_MAX];
  if (!ng_jsmod_resolve_path(path_in, ng_jsmod_current_file(), resolved, sizeof(resolved))) {
    return false;
  }

  for (int i = 0; i < g_jsmod_catalog_n; i++) {
    if (strcmp(g_jsmod_catalog[i].id, id) == 0) {
      if (strcmp(g_jsmod_catalog[i].path, resolved) != 0) {
        NG_LOG_WARN("module: register overwrite id=%s old=%s new=%s", id, g_jsmod_catalog[i].path,
                    resolved);
        strncpy(g_jsmod_catalog[i].path, resolved, sizeof(g_jsmod_catalog[i].path) - 1);
        g_jsmod_catalog[i].path[sizeof(g_jsmod_catalog[i].path) - 1] = '\0';
      }
      return true;
    }
  }
  if (g_jsmod_catalog_n >= NG_JSMOD_CATALOG_MAX) {
    NG_LOG_ERROR("module: catalog full (max %d)", NG_JSMOD_CATALOG_MAX);
    return false;
  }
  NgJsModEntry *e = &g_jsmod_catalog[g_jsmod_catalog_n++];
  memset(e, 0, sizeof(*e));
  strncpy(e->id, id, sizeof(e->id) - 1);
  strncpy(e->path, resolved, sizeof(e->path) - 1);
  NG_LOG_INFO("module: register id=%s path=%s", e->id, e->path);
  return true;
}

const char *ng_jsmod_lookup(const char *id) {
  if (!id || id[0] == '\0') {
    return NULL;
  }
  for (int i = 0; i < g_jsmod_catalog_n; i++) {
    if (strcmp(g_jsmod_catalog[i].id, id) == 0) {
      return g_jsmod_catalog[i].path;
    }
  }
  return NULL;
}

// agent: composer-2.5 | 2026-08-01 | js module catalog path resolve | dea7c1
// agent: composer-2.5 | 2026-08-02 | preserve abs res root slash | b81da8
