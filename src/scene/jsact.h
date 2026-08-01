// agent: composer-2.5 | 2026-08-01 | jsact registry and apply | ecdc2a
#ifndef NG_SCENE_JSACT_H
#define NG_SCENE_JSACT_H

#include "vendor/duktape.h"
#include <stdbool.h>
#include <stdint.h>

#define NG_JSACT_MAX 32
#define NG_JSACT_NAME_MAX 48

uint16_t ng_jsact_hash_name(const char *name);

/* Register receiver+method name on this duk heap (stash). */
bool ng_jsact_register(duk_context *ctx, duk_idx_t receiver_idx, const char *name);

/* Lookup id → push receiver object; returns method name into out_name. */
bool ng_jsact_lookup(duk_context *ctx, uint16_t id, char *out_name, size_t out_cap);

void ng_jsact_clear(duk_context *ctx);

/* Apply all peers' actions for tick on this heap (phys-owner). */
void ng_jsact_dispatch_tick(duk_context *ctx, uint32_t tick);

/* Immediate pcall_method for solo / non-lockstep. */
bool ng_jsact_call(duk_context *ctx, const char *name, uint8_t argc, const float *argv);

uint32_t ng_jsact_apply_peer(void);
uint32_t ng_jsact_apply_tick(void);

#endif
// agent: composer-2.5 | 2026-08-01 | jsact registry and apply | ecdc2a
