// agent: composer-2.5 | 2026-07-25 | duktape script VM | 3e9a5d
#include "ng_script.h"
#include "ng_bind.h"
#include <raylib.h>
#include <string.h>

bool ng_script_init(NgScript *script) {
  if (!script) {
    return false;
  }
  script->ctx = duk_create_heap_default();
  if (!script->ctx) {
    TraceLog(LOG_ERROR, "NG: duktape heap creation failed");
    return false;
  }
  ng_bind_register(script->ctx);
  return true;
}

void ng_script_shutdown(NgScript *script) {
  if (!script || !script->ctx) {
    return;
  }
  duk_destroy_heap(script->ctx);
  script->ctx = NULL;
}

bool ng_script_run(NgScript *script, const char *source) {
  if (!script || !script->ctx || !source) {
    return false;
  }
  duk_push_string(script->ctx, source);
  if (duk_peval(script->ctx) != DUK_EXEC_SUCCESS) {
    TraceLog(LOG_ERROR, "NG: script error: %s", duk_safe_to_string(script->ctx, -1));
    duk_pop(script->ctx);
    return false;
  }
  duk_pop(script->ctx);
  return true;
}

bool ng_script_run_file(NgScript *script, const char *path) {
  char *text = LoadFileText(path);
  if (!text) {
    TraceLog(LOG_ERROR, "NG: script file not found: %s", path);
    return false;
  }
  const bool ok = ng_script_run(script, text);
  UnloadFileText(text);
  return ok;
}
