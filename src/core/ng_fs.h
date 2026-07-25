// agent: composer-2.5 | 2026-07-25 | read text file helper | b7e41d
#ifndef NG_FS_H
#define NG_FS_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef NG_SERVER
static inline char *ng_fs_read_text(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    return NULL;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  const long sz = ftell(f);
  if (sz < 0) {
    fclose(f);
    return NULL;
  }
  rewind(f);
  char *buf = (char *)malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  const size_t n = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  buf[n] = '\0';
  return buf;
}

static inline void ng_fs_free_text(char *text) { free(text); }
#else
#include <raylib.h>
static inline char *ng_fs_read_text(const char *path) { return LoadFileText(path); }
static inline void ng_fs_free_text(char *text) { UnloadFileText(text); }
#endif

#endif
