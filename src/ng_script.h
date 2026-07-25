// agent: composer-2.5 | 2026-07-25 | duktape script VM | 3e9a5d
#ifndef NG_SCRIPT_H
#define NG_SCRIPT_H

#include "vendor/duktape.h"
#include <stdbool.h>

typedef struct NgScript {
  duk_context *ctx;
} NgScript;

bool ng_script_init(NgScript *script);
void ng_script_shutdown(NgScript *script);
bool ng_script_run_file(NgScript *script, const char *path);
bool ng_script_run(NgScript *script, const char *source);

#endif
