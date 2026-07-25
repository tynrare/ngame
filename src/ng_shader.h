// agent: composer-2.5 | 2026-07-25 | shader load and uniforms | 2d8f6b
#ifndef NG_SHADER_H
#define NG_SHADER_H

#include <raylib.h>

typedef struct NgShader {
  Shader handle;
  int loc_time;
  int loc_resolution;
  int loc_tint;
} NgShader;

NgShader ng_shader_load(const char *vs_path, const char *fs_path);
void ng_shader_unload(NgShader *shader);
void ng_shader_set_common(NgShader *shader, float time);
void ng_shader_poll(void);

#endif
