// agent: composer-2.5 | 2026-08-01 | jsact registry and apply | 30972d
// agent: composer-2.5 | 2026-08-01 | jsact sim entity id seq | 09de0e
#include "jsact.h"
#include "engine/ng_log.h"
#include "engine/ng_proto.h"
#include "graph.h"
#include "lockstep.h"
#include <stdio.h>
#include <string.h>

typedef struct NgJsActEntry {
  bool used;
  uint16_t id;
  char name[NG_JSACT_NAME_MAX];
  char stash_key[64];
} NgJsActEntry;

static NgJsActEntry g_jsact[NG_JSACT_MAX];
static int g_jsact_n;
static uint32_t g_jsact_apply_peer;
static uint32_t g_jsact_apply_tick;
static uint8_t g_jsact_spawn_seq;

uint16_t ng_jsact_hash_name(const char *name) {
  /* FNV-1a 16-bit (xor-fold). */
  uint32_t h = 2166136261u;
  if (!name) {
    return 0;
  }
  for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
    h ^= (uint32_t)(*p);
    h *= 16777619u;
  }
  return (uint16_t)((h >> 16) ^ (h & 0xffffu));
}

uint32_t ng_jsact_apply_peer(void) { return g_jsact_apply_peer; }
uint32_t ng_jsact_apply_tick(void) { return g_jsact_apply_tick; }

uint32_t ng_jsact_next_sim_entity_id(void) {
  const uint8_t seq = g_jsact_spawn_seq;
  if (g_jsact_spawn_seq < 0x0fu) {
    g_jsact_spawn_seq++;
  } else {
    NG_LOG_WARN("action: sim spawn seq saturated peer=%u tick=%u", g_jsact_apply_peer,
                g_jsact_apply_tick);
  }
  return mod_scene_graph_pack_sim_id(g_jsact_apply_tick, g_jsact_apply_peer, seq);
}

static NgJsActEntry *ng_jsact_find(uint16_t id) {
  for (int i = 0; i < g_jsact_n; i++) {
    if (g_jsact[i].used && g_jsact[i].id == id) {
      return &g_jsact[i];
    }
  }
  return NULL;
}

static NgJsActEntry *ng_jsact_find_name(const char *name) {
  for (int i = 0; i < g_jsact_n; i++) {
    if (g_jsact[i].used && strcmp(g_jsact[i].name, name) == 0) {
      return &g_jsact[i];
    }
  }
  return NULL;
}

bool ng_jsact_register(duk_context *ctx, duk_idx_t receiver_idx, const char *name) {
  if (!ctx || !name || name[0] == '\0' || !duk_is_object(ctx, receiver_idx)) {
    NG_LOG_ERROR("action_register: need (receiver, name)");
    return false;
  }
  duk_get_prop_string(ctx, receiver_idx, name);
  if (!duk_is_function(ctx, -1)) {
    duk_pop(ctx);
    NG_LOG_ERROR("action_register: missing method %s", name);
    return false;
  }
  duk_pop(ctx);

  const uint16_t id = ng_jsact_hash_name(name);
  NgJsActEntry *e = ng_jsact_find_name(name);
  if (!e) {
    e = ng_jsact_find(id);
    if (e && strcmp(e->name, name) != 0) {
      NG_LOG_ERROR("action_register: hash collision %s vs %s", name, e->name);
      return false;
    }
  }
  if (!e) {
    if (g_jsact_n >= NG_JSACT_MAX) {
      NG_LOG_ERROR("action_register: registry full");
      return false;
    }
    e = &g_jsact[g_jsact_n++];
    memset(e, 0, sizeof(*e));
    e->used = true;
    e->id = id;
    strncpy(e->name, name, sizeof(e->name) - 1);
    snprintf(e->stash_key, sizeof(e->stash_key), "ng_jsact_%u", (unsigned)id);
  } else {
    NG_LOG_WARN("action_register: overwrite %s", name);
  }

  duk_push_global_stash(ctx);
  duk_dup(ctx, receiver_idx);
  duk_put_prop_string(ctx, -2, e->stash_key);
  duk_pop(ctx);
  NG_LOG_INFO("action_register: %s id=%u", name, (unsigned)id);
  return true;
}

bool ng_jsact_lookup(duk_context *ctx, uint16_t id, char *out_name, size_t out_cap) {
  NgJsActEntry *e = ng_jsact_find(id);
  if (!e || !ctx) {
    return false;
  }
  duk_push_global_stash(ctx);
  duk_get_prop_string(ctx, -1, e->stash_key);
  duk_remove(ctx, -2);
  if (!duk_is_object(ctx, -1)) {
    duk_pop(ctx);
    return false;
  }
  if (out_name && out_cap > 0) {
    strncpy(out_name, e->name, out_cap - 1);
    out_name[out_cap - 1] = '\0';
  }
  return true;
}

void ng_jsact_clear(duk_context *ctx) {
  if (ctx) {
    duk_push_global_stash(ctx);
    for (int i = 0; i < g_jsact_n; i++) {
      if (g_jsact[i].used) {
        duk_del_prop_string(ctx, -1, g_jsact[i].stash_key);
      }
    }
    duk_pop(ctx);
  }
  /* Keep name→id table across dual server/view heaps; receivers live in stash. */
  g_jsact_apply_peer = 0;
  g_jsact_apply_tick = 0;
  g_jsact_spawn_seq = 0;
}

static bool ng_jsact_pcall(duk_context *ctx, const char *name, uint8_t argc, const float *argv) {
  const uint16_t id = ng_jsact_hash_name(name);
  char nbuf[NG_JSACT_NAME_MAX];
  if (!ng_jsact_lookup(ctx, id, nbuf, sizeof(nbuf))) {
    NG_LOG_WARN("action: unknown %s", name);
    return false;
  }
  /* stack: receiver */
  duk_get_prop_string(ctx, -1, name);
  if (!duk_is_function(ctx, -1)) {
    duk_pop_n(ctx, 2);
    NG_LOG_ERROR("action: not a function %s", name);
    return false;
  }
  duk_insert(ctx, -2); /* [fn, receiver] for pcall_method */
  for (uint8_t i = 0; i < argc; i++) {
    duk_push_number(ctx, argv[i]);
  }
  if (duk_pcall_method(ctx, (duk_idx_t)argc) != DUK_EXEC_SUCCESS) {
    NG_LOG_ERROR("action %s: %s", name, duk_safe_to_string(ctx, -1));
    duk_pop(ctx);
    return false;
  }
  duk_pop(ctx);
  return true;
}

bool ng_jsact_call(duk_context *ctx, const char *name, uint8_t argc, const float *argv) {
  if (!ctx || !name) {
    return false;
  }
  const NgSpawnCtx prev = mod_scene_spawn_get_ctx();
  mod_scene_spawn_set_ctx(NG_SPAWN_CTX_ACTION_APPLY);
  g_jsact_apply_tick = mod_lockstep_active() ? mod_lockstep_sim_tick() + 1u : 1u;
  if (g_jsact_apply_tick == 0u) {
    g_jsact_apply_tick = 1u;
  }
  g_jsact_apply_peer = mod_lockstep_local_peer_id();
  if (g_jsact_apply_peer == 0u) {
    g_jsact_apply_peer = 1u;
  }
  g_jsact_spawn_seq = 0;
  const bool ok = ng_jsact_pcall(ctx, name, argc, argv);
  g_jsact_apply_peer = 0;
  g_jsact_apply_tick = 0;
  g_jsact_spawn_seq = 0;
  mod_scene_spawn_set_ctx(prev);
  return ok;
}

void ng_jsact_dispatch_tick(duk_context *ctx, uint32_t tick) {
  // agent: composer-2.5 | 2026-08-01 | actions only when confirmed | 1d4a0d
  if (!ctx || tick == 0 || !mod_lockstep_active()) {
    return;
  }
  /* Predict/ZF must not fire oneshots — only confirmed timeline (article anti-drift). */
  if (tick > mod_lockstep_confirmed_tick()) {
    return;
  }
  const NgSpawnCtx prev = mod_scene_spawn_get_ctx();
  mod_scene_spawn_set_ctx(NG_SPAWN_CTX_ACTION_APPLY);
  g_jsact_apply_tick = tick;
  g_jsact_spawn_seq = 0;
  const int pc = mod_lockstep_peer_count();
  for (int i = 0; i < pc; i++) {
    const uint32_t peer = mod_lockstep_peer_id_at(i);
    if (peer == 0) {
      continue;
    }
    NgLockAction act;
    if (!mod_lockstep_action_for(peer, tick, &act) || !act.present) {
      continue;
    }
    NgJsActEntry *e = ng_jsact_find(act.id);
    if (!e) {
      NG_LOG_WARN("action: unregistered id=%u peer=%u tick=%u", (unsigned)act.id, peer, tick);
      continue;
    }
    g_jsact_apply_peer = peer;
    (void)ng_jsact_pcall(ctx, e->name, act.argc, act.argv);
  }
  g_jsact_apply_peer = 0;
  g_jsact_apply_tick = 0;
  g_jsact_spawn_seq = 0;
  mod_scene_spawn_set_ctx(prev);
}
// agent: composer-2.5 | 2026-08-01 | jsact registry and apply | 30972d
// agent: composer-2.5 | 2026-08-01 | jsact sim entity id seq | 09de0e
// agent: composer-2.5 | 2026-08-01 | actions only when confirmed | 1d4a0d
