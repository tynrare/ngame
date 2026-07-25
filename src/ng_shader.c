// agent: composer-2.5 | 2026-07-25 | shader load and uniforms | 2d8f6b
#include "ng_shader.h"
#include <raylib.h>
#include <stdio.h>
#include <string.h>

#if defined(PLATFORM_DESKTOP)
#define NG_GLSL 330
#elif defined(GRAPHICS_API_OPENGL_ES3)
#define NG_GLSL 300
#else
#define NG_GLSL 100
#endif

static char *ng_shader_prepend_version(const char *source) {
  const char *header;
#if defined(PLATFORM_DESKTOP)
  header = "#version 330\n";
#elif defined(GRAPHICS_API_OPENGL_ES3)
  header = "#version 300 es\n"
           "precision mediump float;\n";
#else
  header = "#version 100\n";
#endif

  const size_t header_len = strlen(header);
  const size_t source_len = strlen(source);
  char *out = (char *)MemAlloc(header_len + source_len + 1);
  if (!out) {
    return NULL;
  }
  strcpy(out, header);
  strcat(out, source);
  return out;
}

static char *ng_shader_read(const char *path) {
  char *text = LoadFileText(path);
  if (!text) {
    TraceLog(LOG_ERROR, "NG: shader file not found: %s", path);
  }
  return text;
}

NgShader ng_shader_load(const char *vs_path, const char *fs_path) {
  NgShader out = {0};

  char *vs_src = ng_shader_read(vs_path);
  char *fs_src = ng_shader_read(fs_path);
  if (!vs_src || !fs_src) {
    UnloadFileText(vs_src);
    UnloadFileText(fs_src);
    return out;
  }

  char *vs_full = ng_shader_prepend_version(vs_src);
  char *fs_full = ng_shader_prepend_version(fs_src);
  UnloadFileText(vs_src);
  UnloadFileText(fs_src);

  if (!vs_full || !fs_full) {
    MemFree(vs_full);
    MemFree(fs_full);
    return out;
  }

  out.handle = LoadShaderFromMemory(vs_full, fs_full);
  MemFree(vs_full);
  MemFree(fs_full);

  if (out.handle.id == 0) {
    TraceLog(LOG_ERROR, "NG: failed to compile shader: %s + %s", vs_path, fs_path);
    return out;
  }

  out.loc_time = GetShaderLocation(out.handle, "ng_time");
  out.loc_resolution = GetShaderLocation(out.handle, "ng_resolution");
  out.loc_tint = GetShaderLocation(out.handle, "ng_tint");

  return out;
}

void ng_shader_unload(NgShader *shader) {
  if (!shader || shader->handle.id == 0) {
    return;
  }
  UnloadShader(shader->handle);
  *shader = (NgShader){0};
}

void ng_shader_set_common(NgShader *shader, float time) {
  if (!shader || shader->handle.id == 0) {
    return;
  }
  if (shader->loc_time >= 0) {
    SetShaderValue(shader->handle, shader->loc_time, &time, SHADER_UNIFORM_FLOAT);
  }
  if (shader->loc_resolution >= 0) {
    const float res[2] = {(float)GetScreenWidth(), (float)GetScreenHeight()};
    SetShaderValue(shader->handle, shader->loc_resolution, res, SHADER_UNIFORM_VEC2);
  }
}

void ng_shader_poll(void) {
  /* hot-reload hook for later */
}
