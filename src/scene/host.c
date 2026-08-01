// agent: composer-2.5 | 2026-07-27 | js scene lifecycle host | f3a4b5
// agent: composer-2.5 | 2026-07-29 | host server view split | 1b39ad
#include "scene.h"
#include "scene/runtime.h"
#include "engine/ng_fs.h"
#include "engine/ng_log.h"
#include "engine/ng_session.h"
#include "engine/ng_sync.h"
#include "client/input.h"
#include "scene/graph.h"
#include "scene/assets.h"
#include "scene/native.h"
#include "scene/physics.h"
#include "scene/lockstep.h"
#include "scene/jsmod.h"
#include "scene/jsact.h"
#include "engine/ng_proto.h"
#include "engine/ng_mod.h"
#include "net/mod_net.h"
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
#include "server/sim.h"
#endif
#include "world/ng_world.h"
#if !defined(NG_SERVER)
#include "client/render.h"
// agent: codex-5.3 | 2026-07-29 | enable client-side raycast api | 9c4a01
#include <raylib.h>
#endif
#include "ng_path.h"
#include "vendor/duktape.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define NG_SCENE_STASH_KEY "ng_scene"
#define NG_SCENE_PENDING_MODULE "ng_pending_module"
#define NG_SCENE_ACTIVE() (mod_scene_runtime_scene())

static int g_jsmod_eval_depth;

#if defined(NG_SERVER)
void mod_render_apply_session(const NgSessionState *session) { (void)session; }
#endif

static bool mod_scene_is_server(void) {
#ifdef NG_SERVER
  return true;
#else
  return NG_SCENE_ACTIVE()->is_server_host;
#endif
}

// agent: composer-2.5 | 2026-07-29 | lockstep scene sim flag | b3f626
static NgFixedGate mod_scene_lockstep_fixed_gate(void) {
  if (!mod_lockstep_active()) {
    return NG_FIXED_GATE_GO;
  }
  const NgLockGate g = mod_lockstep_gate();
  if (g == NG_LOCK_GATE_BUFFER) {
    return NG_FIXED_GATE_BUFFER;
  }
  if (g == NG_LOCK_GATE_STALL) {
    return NG_FIXED_GATE_STALL;
  }
  return NG_FIXED_GATE_GO;
}

static void mod_scene_lockstep_set_owner_role(void) {
  // agent: composer-2.5 | 2026-07-30 | set lockstep clock owner | d24ebb
  /* Dedicated server always owns the clock. Clients mirror unless solo/auth gateway. */
#if defined(NG_SERVER)
  mod_lockstep_set_clock_owner(true);
#elif defined(NG_HAS_EMBEDDED)
  mod_lockstep_set_clock_owner(mod_net_is_authoritative());
#else
  mod_lockstep_set_clock_owner(false);
#endif
}

static void mod_scene_lockstep_activate(uint32_t local_peer) {
  // agent: composer-2.5 | 2026-07-30 | lockstep ignore entity sync | ae273c
  // agent: composer-2.5 | 2026-07-30 | lockstep activate no wipe | bb830d
  // agent: composer-2.5 | 2026-07-30 | lockstep restart on load | 0de823
  // agent: composer-2.5 | 2026-07-30 | lockstep join fixes logging | 486633
  // agent: composer-2.5 | 2026-07-30 | set lockstep clock owner | d24ebb
  /* Mid-session re-activate: keep rings. Scene load calls restart instead. */
  if (mod_lockstep_active()) {
#if defined(NG_SERVER)
    /* Dedicated server is clock owner with no player peer slot. */
    (void)local_peer;
    if (mod_lockstep_local_peer_id() != 0) {
      /* keep existing; restart path clears */
    }
#else
    if (mod_lockstep_local_peer_id() == 0) {
      mod_lockstep_set_local_peer(local_peer ? local_peer : 1u);
    }
#endif
    mod_scene_lockstep_set_owner_role();
    // agent: composer-2.5 | 2026-08-01 | input sim family checks | 1297a4
    mod_lockstep_set_hybrid(mod_scene_physics_is_hybrid());
    ng_mod_set_fixed_gate(mod_scene_lockstep_fixed_gate);
    NG_LOG_INFO("lockstep: activate keep-alive peer=%u owner=%d syncing=%d await=%d tick=%u",
                mod_lockstep_local_peer_id(), mod_lockstep_is_clock_owner() ? 1 : 0,
                mod_lockstep_syncing() ? 1 : 0, mod_lockstep_awaiting_phys() ? 1 : 0,
                mod_lockstep_sim_tick());
    return;
  }
  mod_lockstep_set_active(true);
  // agent: composer-2.5 | 2026-08-01 | input sim family checks | 1297a4
  mod_lockstep_set_hybrid(mod_scene_physics_is_hybrid());
  mod_lockstep_clear_peers();
#if defined(NG_SERVER)
  /* No local player peer — clients own peer ids from SESSION.your_id. */
  (void)local_peer;
  mod_lockstep_set_local_peer(0);
#else
  mod_lockstep_set_local_peer(local_peer ? local_peer : 1u);
#endif
  mod_scene_lockstep_set_owner_role();
  ng_mod_set_fixed_gate(mod_scene_lockstep_fixed_gate);
  NG_LOG_INFO("lockstep: activate new peer=%u owner=%d syncing=%d await=%d tick=%u",
              mod_lockstep_local_peer_id(), mod_lockstep_is_clock_owner() ? 1 : 0,
              mod_lockstep_syncing() ? 1 : 0, mod_lockstep_awaiting_phys() ? 1 : 0,
              mod_lockstep_sim_tick());
}

static void mod_scene_lockstep_restart(uint32_t local_peer) {
  // agent: composer-2.5 | 2026-07-30 | lockstep restart on load | 0de823
  /* Fresh lockstep clock on scene load — independent of ng_mod fixed tick. */
  mod_lockstep_reset();
  ng_mod_set_fixed_gate(NULL);
  mod_scene_lockstep_activate(local_peer);
}

static void mod_scene_lockstep_maybe_activate(uint32_t local_peer) {
  // agent: composer-2.5 | 2026-08-01 | input sim family checks | 1297a4
  if (mod_scene_physics_is_input_sim()) {
    mod_scene_lockstep_activate(local_peer);
  }
}

static void mod_scene_lockstep_maybe_restart(uint32_t local_peer) {
  // agent: composer-2.5 | 2026-08-01 | input sim family checks | 1297a4
  if (mod_scene_physics_is_input_sim()) {
    mod_scene_lockstep_restart(local_peer);
  }
}

static ModSceneCtx *mod_scene_from_ctx(duk_context *ctx) {
  (void)ctx;
  return mod_scene_runtime_scene();
}

static NgSceneInst *mod_scene_inst_from_handle(int handle) {
  return mod_scene_graph_inst_by_handle(handle);
}

// agent: composer-2.5 | 2026-07-30 | fix stash_func validity gate | 884396
static int mod_scene_stash_func(duk_context *ctx, const char *name) {
  /* Returns >=0 when func_<name> exists on the global stash. Callers only use
   * the value as a presence gate; graph_spawn looks the function up by name. */
  duk_push_global_stash(ctx);
  char key[48];
  snprintf(key, sizeof(key), "func_%s", name);
  duk_get_prop_string(ctx, -1, key);
  const bool ok = duk_is_function(ctx, -1);
  duk_pop_n(ctx, 2); /* func + stash */
  return ok ? 1 : -1;
}

static void mod_scene_stash_put_func(duk_context *ctx, const char *name) {
  duk_push_global_stash(ctx);
  char key[48];
  snprintf(key, sizeof(key), "func_%s", name);
  duk_dup(ctx, -2);
  duk_put_prop_string(ctx, -2, key);
  duk_pop(ctx);
}

static float mod_scene_read_opt_number(duk_context *ctx, int obj_idx, const char *key, float fallback) {
  duk_get_prop_string(ctx, obj_idx, key);
  const float v = duk_is_number(ctx, -1) ? (float)duk_get_number(ctx, -1) : fallback;
  duk_pop(ctx);
  return v;
}

static bool mod_scene_read_opt_vec3(duk_context *ctx, int obj_idx, const char *key, float out[3]) {
  duk_get_prop_string(ctx, obj_idx, key);
  if (!duk_is_object(ctx, -1)) {
    duk_pop(ctx);
    return false;
  }
  const int vidx = duk_get_top_index(ctx);
  out[0] = mod_scene_read_opt_number(ctx, vidx, "x", 0.0f);
  out[1] = mod_scene_read_opt_number(ctx, vidx, "y", 0.0f);
  out[2] = mod_scene_read_opt_number(ctx, vidx, "z", 0.0f);
  duk_pop(ctx);
  return true;
}

static bool mod_scene_parse_view_describe(duk_context *ctx, int obj_idx) {
  NgSceneViewMeta view = {0};
  view.valid = true;
  view.bg_r = 0;
  view.bg_g = 0;
  view.bg_b = 0;
  view.camera_mode = NG_SCENE_CAM_FIXED;
  view.cam_fovy = 45.0f;
  view.orbit_radius = 6.0f;
  view.orbit_speed = 0.6f;
  view.orbit_height = 2.0f;
  view.cam_pos[2] = 6.0f;
  view.cam_target[0] = view.cam_target[1] = view.cam_target[2] = 0.0f;

  duk_get_prop_string(ctx, obj_idx, "bg");
  if (duk_is_object(ctx, -1)) {
    const int bg_idx = duk_get_top_index(ctx);
    view.bg_r = (uint8_t)mod_scene_read_opt_number(ctx, bg_idx, "r", 0.0f);
    view.bg_g = (uint8_t)mod_scene_read_opt_number(ctx, bg_idx, "g", 0.0f);
    view.bg_b = (uint8_t)mod_scene_read_opt_number(ctx, bg_idx, "b", 0.0f);
  }
  duk_pop(ctx);

  duk_get_prop_string(ctx, obj_idx, "camera");
  if (duk_is_object(ctx, -1)) {
    const int cam_idx = duk_get_top_index(ctx);
    duk_get_prop_string(ctx, cam_idx, "mode");
    if (duk_is_string(ctx, -1) && strcmp(duk_get_string(ctx, -1), "orbit") == 0) {
      view.camera_mode = NG_SCENE_CAM_ORBIT;
    }
    duk_pop(ctx);
    (void)mod_scene_read_opt_vec3(ctx, cam_idx, "position", view.cam_pos);
    (void)mod_scene_read_opt_vec3(ctx, cam_idx, "target", view.cam_target);
    view.cam_fovy = mod_scene_read_opt_number(ctx, cam_idx, "fovy", view.cam_fovy);
    duk_get_prop_string(ctx, cam_idx, "orbit");
    if (duk_is_object(ctx, -1)) {
      const int orb_idx = duk_get_top_index(ctx);
      view.orbit_radius = mod_scene_read_opt_number(ctx, orb_idx, "radius", view.orbit_radius);
      view.orbit_speed = mod_scene_read_opt_number(ctx, orb_idx, "speed", view.orbit_speed);
      view.orbit_height = mod_scene_read_opt_number(ctx, orb_idx, "height", view.orbit_height);
    }
    duk_pop(ctx);
  }
  duk_pop(ctx);
  // agent: composer-2.5 | 2026-07-29 | lockstep scene sim flag | b3f626
  // agent: composer-2.5 | 2026-08-01 | hybrid sim js parse | 7b3cfd
  duk_get_prop_string(ctx, obj_idx, "sim");
  if (duk_is_string(ctx, -1)) {
    const char *sim = duk_get_string(ctx, -1);
    if (strcmp(sim, "lockstep") == 0) {
      mod_scene_physics_set_sim_mode(NG_PHYS_SIM_LOCKSTEP);
    } else if (strcmp(sim, "hybrid") == 0) {
      mod_scene_physics_set_sim_mode(NG_PHYS_SIM_HYBRID);
    }
  }
  duk_pop(ctx);
  // agent: composer-2.5 | 2026-07-30 | gravity vel mass js api | f956eb
  duk_get_prop_string(ctx, obj_idx, "gravity");
  if (duk_is_object(ctx, -1)) {
    float g[3] = {0.0f, 0.0f, 0.0f};
    duk_get_prop_string(ctx, -1, "x");
    if (duk_is_number(ctx, -1)) {
      g[0] = (float)duk_get_number(ctx, -1);
    }
    duk_pop(ctx);
    duk_get_prop_string(ctx, -1, "y");
    if (duk_is_number(ctx, -1)) {
      g[1] = (float)duk_get_number(ctx, -1);
    }
    duk_pop(ctx);
    duk_get_prop_string(ctx, -1, "z");
    if (duk_is_number(ctx, -1)) {
      g[2] = (float)duk_get_number(ctx, -1);
    }
    duk_pop(ctx);
    mod_scene_physics_set_gravity(g[0], g[1], g[2]);
  }
  duk_pop(ctx);
  return mod_scene_assets_describe_view(&view);
}

static bool mod_scene_lockstep_body(const char *body) {
  // agent: composer-2.5 | 2026-08-01 | input sim family checks | 1297a4
  return mod_scene_physics_is_input_sim() && body && body[0] != '\0';
}

static bool mod_scene_spawn_creates_local(NgSyncMode sync, bool on_server, bool is_controller,
                                          bool lockstep_body) {
  // agent: composer-2.5 | 2026-07-30 | lockstep ignore entity sync | ae273c
  if (lockstep_body) {
    (void)sync;
    (void)on_server;
    (void)is_controller;
    return true;
  }
  if (sync == NG_SYNC_SERVER) {
    return on_server;
  }
  if (sync == NG_SYNC_SHARED || sync == NG_SYNC_LOCAL) {
    return !on_server;
  }
  if (sync == NG_SYNC_OWNER) {
    return !on_server && is_controller;
  }
  return false;
}

static duk_ret_t bind_describe(duk_context *ctx) {
  const char *kind = duk_require_string(ctx, 0);
  const char *name = duk_require_string(ctx, 1);
  NgSyncMode sync = NG_SYNC_SERVER;
  const char *model = NULL;
  const char *body = NULL;
  int func_idx = -1;

  if (duk_is_object(ctx, 2)) {
    duk_get_prop_string(ctx, 2, "sync");
    if (duk_is_string(ctx, -1)) {
      sync = ng_sync_mode_parse(duk_get_string(ctx, -1));
    }
    duk_pop(ctx);
    duk_get_prop_string(ctx, 2, "mode");
    if (duk_is_string(ctx, -1)) {
      sync = ng_sync_mode_parse(duk_get_string(ctx, -1));
    }
    duk_pop(ctx);
    if (strcmp(kind, "mesh") == 0) {
      float w = 1.0f, h = 1.0f, d = 1.0f;
      const char *shape = "cube";
      duk_get_prop_string(ctx, 2, "width");
      if (duk_is_number(ctx, -1)) {
        w = (float)duk_get_number(ctx, -1);
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "height");
      if (duk_is_number(ctx, -1)) {
        h = (float)duk_get_number(ctx, -1);
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "depth");
      if (duk_is_number(ctx, -1)) {
        d = (float)duk_get_number(ctx, -1);
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "shape");
      if (duk_is_string(ctx, -1)) {
        shape = duk_get_string(ctx, -1);
      }
      duk_pop(ctx);
      // agent: composer-2.5 | 2026-07-28 | wire js shape into mesh describe | f1a2b3
      mod_scene_assets_describe_mesh(name, shape, w, h, d);
    } else if (strcmp(kind, "shader") == 0) {
      const char *fragment = NULL;
      const char *vertex = NULL;
      uint8_t tr = 255, tg = 255, tb = 255;
      bool have_tint = false;
      duk_get_prop_string(ctx, 2, "fragment");
      if (duk_is_string(ctx, -1)) {
        fragment = duk_get_string(ctx, -1);
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "vertex");
      if (duk_is_string(ctx, -1)) {
        vertex = duk_get_string(ctx, -1);
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "tint");
      if (duk_is_object(ctx, -1)) {
        const int tidx = duk_get_top_index(ctx);
        tr = (uint8_t)mod_scene_read_opt_number(ctx, tidx, "r", 255.0f);
        tg = (uint8_t)mod_scene_read_opt_number(ctx, tidx, "g", 255.0f);
        tb = (uint8_t)mod_scene_read_opt_number(ctx, tidx, "b", 255.0f);
        have_tint = true;
      }
      duk_pop(ctx);
      mod_scene_assets_describe_shader(name, fragment, vertex, tr, tg, tb, have_tint);
    } else if (strcmp(kind, "model") == 0) {
      const char *mesh = NULL;
      const char *shader = NULL;
      duk_get_prop_string(ctx, 2, "mesh");
      if (duk_is_string(ctx, -1)) {
        mesh = duk_get_string(ctx, -1);
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "shader");
      if (duk_is_string(ctx, -1)) {
        shader = duk_get_string(ctx, -1);
      }
      duk_pop(ctx);
      mod_scene_assets_describe_model(name, mesh, shader);
    } else if (strcmp(kind, "shape") == 0) {
      // agent: composer-2.5 | 2026-07-29 | body shape fixed_step wire | 37245c
      // agent: composer-2.5 | 2026-07-30 | gravity vel mass js api | f956eb
      const char *stype = "box";
      float hx = 0.5f, hy = 0.5f, hz = 0.5f, density = 1.0f, friction = 0.3f;
      bool sensor = false;
      duk_get_prop_string(ctx, 2, "type");
      if (duk_is_string(ctx, -1)) {
        stype = duk_get_string(ctx, -1);
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "hx");
      if (duk_is_number(ctx, -1)) {
        hx = (float)duk_get_number(ctx, -1);
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "hy");
      if (duk_is_number(ctx, -1)) {
        hy = (float)duk_get_number(ctx, -1);
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "hz");
      if (duk_is_number(ctx, -1)) {
        hz = (float)duk_get_number(ctx, -1);
      }
      duk_pop(ctx);
      // agent: composer-2.5 | 2026-07-30 | sphere describe parse | 741115
      duk_get_prop_string(ctx, 2, "radius");
      if (duk_is_number(ctx, -1)) {
        hx = (float)duk_get_number(ctx, -1);
        hy = hx;
        hz = hx;
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "density");
      if (duk_is_number(ctx, -1)) {
        density = (float)duk_get_number(ctx, -1);
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "friction");
      if (duk_is_number(ctx, -1)) {
        friction = (float)duk_get_number(ctx, -1);
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "sensor");
      if (duk_is_boolean(ctx, -1)) {
        sensor = duk_get_boolean(ctx, -1) ? true : false;
      }
      duk_pop(ctx);
      mod_scene_physics_describe_shape(name, stype, hx, hy, hz, density, friction, sensor);
    } else if (strcmp(kind, "body") == 0) {
      const char *btype = "static";
      const char *shape = NULL;
      duk_get_prop_string(ctx, 2, "type");
      if (duk_is_string(ctx, -1)) {
        btype = duk_get_string(ctx, -1);
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "shape");
      if (duk_is_string(ctx, -1)) {
        shape = duk_get_string(ctx, -1);
      }
      duk_pop(ctx);
      mod_scene_physics_describe_body(name, btype, shape);
    } else if (strcmp(kind, "entity") == 0) {
      duk_get_prop_string(ctx, 2, "model");
      if (duk_is_string(ctx, -1)) {
        model = duk_get_string(ctx, -1);
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "body");
      if (duk_is_string(ctx, -1)) {
        body = duk_get_string(ctx, -1);
      }
      duk_pop(ctx);
      duk_get_prop_string(ctx, 2, "func");
      if (duk_is_function(ctx, -1)) {
        mod_scene_stash_put_func(ctx, name);
        func_idx = mod_scene_stash_func(ctx, name);
      }
      duk_pop(ctx);
    } else if (strcmp(kind, "scene") == 0) {
      // agent: composer-2.5 | 2026-07-28 | js scene view bg camera meta | e2f3a4
      mod_scene_parse_view_describe(ctx, 2);
    }
  }

  if (strcmp(kind, "entity") == 0) {
    if (!model) {
      duk_push_int(ctx, 0);
      return 1;
    }
    mod_scene_graph_describe(kind, name, sync, model, body, func_idx);
  } else if (strcmp(kind, "mesh") == 0 || strcmp(kind, "shader") == 0 ||
             strcmp(kind, "model") == 0 || strcmp(kind, "scene") == 0 ||
             strcmp(kind, "shape") == 0 || strcmp(kind, "body") == 0) {
    mod_scene_graph_describe(kind, name, sync, model, body, func_idx);
  }
  duk_push_int(ctx, 1);
  return 1;
}

static bool mod_scene_call_entity_method(ModSceneCtx *ctx, NgSceneInst *inst, const char *method) {
  if (!ctx->ctx || !inst) {
    return false;
  }
  // agent: composer-2.5 | 2026-07-30 | pop stash after entity method | 7856d9
  char key[48];
  snprintf(key, sizeof(key), "inst_%d", inst->handle);
  duk_push_global_stash(ctx->ctx);
  duk_get_prop_string(ctx->ctx, -1, key);
  if (!duk_is_object(ctx->ctx, -1)) {
    duk_pop_n(ctx->ctx, 2);
    return false;
  }
  duk_get_prop_string(ctx->ctx, -1, method);
  if (!duk_is_function(ctx->ctx, -1)) {
    duk_pop_n(ctx->ctx, 3);
    return false;
  }
  duk_insert(ctx->ctx, -2);
  if (duk_pcall_method(ctx->ctx, 0) != DUK_EXEC_SUCCESS) {
    NG_LOG_ERROR("entity.%s: %s", method, duk_safe_to_string(ctx->ctx, -1));
    duk_pop_n(ctx->ctx, 2); /* error + stash */
    return false;
  }
  duk_pop_n(ctx->ctx, 2); /* result + stash */
  return true;
}

// agent: composer-2.5 | 2026-07-29 | spawn opts key ordinal match | 701175
typedef struct ModSceneSpawnOpts {
  char key[32];
  float pos[3];
  float rot[3];
  float scale;
  bool have_pos;
  bool have_rot;
  bool have_scale;
} ModSceneSpawnOpts;

static void mod_scene_spawn_opts_default(ModSceneSpawnOpts *opts) {
  memset(opts, 0, sizeof(*opts));
  opts->scale = 1.0f;
}

static void mod_scene_read_spawn_opts(duk_context *ctx, int idx, ModSceneSpawnOpts *opts) {
  mod_scene_spawn_opts_default(opts);
  if (!duk_is_object(ctx, idx)) {
    return;
  }
  duk_get_prop_string(ctx, idx, "key");
  if (duk_is_string(ctx, -1)) {
    const char *key = duk_get_string(ctx, -1);
    if (key) {
      strncpy(opts->key, key, sizeof(opts->key) - 1);
    }
  }
  duk_pop(ctx);
  duk_get_prop_string(ctx, idx, "position");
  if (duk_is_object(ctx, -1)) {
    const int pidx = duk_get_top_index(ctx);
    opts->pos[0] = mod_scene_read_opt_number(ctx, pidx, "x", 0.0f);
    opts->pos[1] = mod_scene_read_opt_number(ctx, pidx, "y", 0.0f);
    opts->pos[2] = mod_scene_read_opt_number(ctx, pidx, "z", 0.0f);
    opts->have_pos = true;
  }
  duk_pop(ctx);
  duk_get_prop_string(ctx, idx, "rotation");
  if (duk_is_object(ctx, -1)) {
    const int ridx = duk_get_top_index(ctx);
    opts->rot[0] = mod_scene_read_opt_number(ctx, ridx, "x", 0.0f);
    opts->rot[1] = mod_scene_read_opt_number(ctx, ridx, "y", 0.0f);
    opts->rot[2] = mod_scene_read_opt_number(ctx, ridx, "z", 0.0f);
    opts->have_rot = true;
  } else if (duk_is_number(ctx, -1)) {
    opts->rot[1] = (float)duk_get_number(ctx, -1);
    opts->have_rot = true;
  }
  duk_pop(ctx);
  duk_get_prop_string(ctx, idx, "scale");
  if (duk_is_number(ctx, -1)) {
    opts->scale = (float)duk_get_number(ctx, -1);
    opts->have_scale = true;
  }
  duk_pop(ctx);
}

static bool mod_scene_spawn_should_materialize(NgSyncMode sync, bool on_server,
                                               bool lockstep_body) {
  // agent: composer-2.5 | 2026-07-30 | lockstep ignore entity sync | ae273c
  if (lockstep_body) {
    return true;
  }
  if (sync == NG_SYNC_LOCAL) {
    return false;
  }
  if (sync == NG_SYNC_SERVER) {
    return !on_server;
  }
  // shared + owner: every view materializes for render
  return !on_server;
}

static int mod_scene_finish_spawn(duk_context *ctx, ModSceneCtx *scene, const char *name,
                                  uint32_t entity_id, const char *key, const float pos[3],
                                  const float rot[3], float scale, int func_idx) {
  const int handle =
      mod_scene_graph_spawn(name, entity_id, key, pos, rot, scale, ctx, func_idx);
  NgSceneInst *inst = mod_scene_inst_from_handle(handle);
  if (inst) {
    // agent: composer-2.5 | 2026-07-29 | body shape fixed_step wire | 37245c
    if (inst->body[0] != '\0') {
      mod_scene_physics_attach(handle, inst->body, inst->sync, mod_scene_is_server(),
                               scene->is_controller, inst->pos, inst->rot);
    }
    mod_scene_call_entity_method(scene, inst, "init");
    mod_scene_call_entity_method(scene, inst, "start");
  }
  return handle;
}

static int mod_scene_spawn_from_pending(duk_context *ctx, ModSceneCtx *scene, const char *name,
                                        const NgSessionSpawn *pending, int func_idx) {
  const float scale = pending->scale > 0.0f ? pending->scale : 1.0f;
  return mod_scene_finish_spawn(ctx, scene, name, pending->entity_id, pending->key, pending->pos,
                                pending->rot, scale, func_idx);
}

static int mod_scene_spawn_refresh_existing(ModSceneCtx *scene, NgSceneInst *inst,
                                            const float *pos, const float *rot, float scale,
                                            bool have_pos, bool have_rot, bool have_scale) {
  if (!inst) {
    return 0;
  }
  if (have_pos && pos) {
    inst->pos[0] = pos[0];
    inst->pos[1] = pos[1];
    inst->pos[2] = pos[2];
  }
  if (have_rot && rot) {
    inst->rot[0] = rot[0];
    inst->rot[1] = rot[1];
    inst->rot[2] = rot[2];
  }
  if (have_scale && scale > 0.0f) {
    inst->scale = scale;
  }
  if (inst->body[0] != '\0') {
    if (inst->body_id_bits != 0 && have_pos) {
      mod_scene_physics_detach(inst->handle);
    }
    if (inst->body_id_bits == 0) {
      mod_scene_physics_attach(inst->handle, inst->body, inst->sync, mod_scene_is_server(),
                               scene->is_controller, inst->pos, inst->rot);
    }
  }
  return inst->handle;
}

static duk_ret_t bind_spawn(duk_context *ctx) {
  // agent: composer-2.5 | 2026-08-01 | spawn context sim id guards | 7c6221
  ModSceneCtx *scene = mod_scene_from_ctx(ctx);
  const char *name = duk_require_string(ctx, 0);
  NgSceneDesc *desc = mod_scene_graph_entity_desc(name);
  if (!desc) {
    duk_push_int(ctx, 0);
    return 1;
  }

  ModSceneSpawnOpts opts;
  mod_scene_read_spawn_opts(ctx, 1, &opts);
  const char *key = opts.key[0] ? opts.key : NULL;
  const bool on_server = mod_scene_is_server();
  const NgSyncMode sync = desc->sync;
  const bool lockstep_body = mod_scene_lockstep_body(desc->body);
  const NgSpawnCtx spawn_ctx = mod_scene_spawn_get_ctx();
  const int func_idx = mod_scene_stash_func(ctx, name);
  const float *pos = opts.have_pos ? opts.pos : NULL;
  const float *rot = opts.have_rot ? opts.rot : NULL;
  const float scale = opts.have_scale ? opts.scale : 1.0f;
  float default_pos[3] = {0, 0, 0};
  float default_rot[3] = {0, 0, 0};
  if (!pos) {
    pos = default_pos;
  }
  if (!rot) {
    rot = default_rot;
  }

  /* Local may spawn anytime; net entities only in start / action / join. */
  if (sync != NG_SYNC_LOCAL && spawn_ctx == NG_SPAWN_CTX_NONE) {
    NG_LOG_ERROR("spawn: refuse %s — not in start/action/join context", name);
    duk_push_int(ctx, 0);
    return 1;
  }

  /* Action apply: deterministic sim-band id (idempotent on resim). */
  if (spawn_ctx == NG_SPAWN_CTX_ACTION_APPLY && sync != NG_SYNC_LOCAL) {
    const uint32_t entity_id = ng_jsact_next_sim_entity_id();
    NgSceneInst *by_id = mod_scene_graph_inst_by_id(entity_id);
    if (by_id) {
      duk_push_int(ctx, mod_scene_spawn_refresh_existing(scene, by_id, pos, rot, scale,
                                                         opts.have_pos, opts.have_rot,
                                                         opts.have_scale));
      return 1;
    }
    if (key) {
      NgSceneInst *by_key = mod_scene_graph_inst_by_key(key);
      if (by_key) {
        duk_push_int(ctx, mod_scene_spawn_refresh_existing(scene, by_key, pos, rot, scale,
                                                           opts.have_pos, opts.have_rot,
                                                           opts.have_scale));
        return 1;
      }
    }
    if (!mod_scene_spawn_creates_local(sync, on_server, scene->is_controller, lockstep_body) &&
        !mod_scene_spawn_should_materialize(sync, on_server, lockstep_body) && !lockstep_body) {
      mod_scene_graph_registry_add_instance(name, key, entity_id, sync, pos, rot, scale);
      duk_push_int(ctx, 0);
      return 1;
    }
    const int handle =
        mod_scene_finish_spawn(ctx, scene, name, entity_id, key, pos, rot, scale, func_idx);
    duk_push_int(ctx, handle);
    return 1;
  }

  // agent: composer-2.5 | 2026-08-01 | keyed spawn reattach pose | 35782a
  if (key) {
    NgSceneInst *by_key = mod_scene_graph_inst_by_key(key);
    if (by_key) {
      duk_push_int(ctx, mod_scene_spawn_refresh_existing(scene, by_key, pos, rot, scale,
                                                         opts.have_pos, opts.have_rot,
                                                         opts.have_scale));
      return 1;
    }
  }

  const NgSessionSpawn *pending = mod_scene_graph_take_pending(name, key);
  if (pending) {
    if (mod_scene_spawn_creates_local(sync, on_server, scene->is_controller, lockstep_body) ||
        mod_scene_spawn_should_materialize(sync, on_server, lockstep_body)) {
      const int handle = mod_scene_spawn_from_pending(ctx, scene, name, pending, func_idx);
      duk_push_int(ctx, handle);
      return 1;
    }
    mod_scene_graph_registry_add_instance(name, pending->key[0] ? pending->key : key,
                                          pending->entity_id, sync, pending->pos, pending->rot,
                                          pending->scale > 0.0f ? pending->scale : 1.0f);
    duk_push_int(ctx, 0);
    return 1;
  }

  if (!mod_scene_spawn_creates_local(sync, on_server, scene->is_controller, lockstep_body)) {
    if (sync == NG_SYNC_LOCAL) {
      /* Wrong heap for local create. */
      duk_push_int(ctx, 0);
      return 1;
    }
    const uint32_t entity_id = mod_scene_graph_alloc_id();
    mod_scene_graph_registry_add_instance(name, key, entity_id, sync, pos, rot, scale);
    if (mod_scene_spawn_should_materialize(sync, on_server, lockstep_body)) {
      const int handle =
          mod_scene_finish_spawn(ctx, scene, name, entity_id, key, pos, rot, scale, func_idx);
      duk_push_int(ctx, handle);
      return 1;
    }
    duk_push_int(ctx, 0);
    return 1;
  }

  uint32_t entity_id = 0;
  if (sync == NG_SYNC_LOCAL) {
    entity_id = mod_scene_graph_alloc_local_id();
  }
  const int handle =
      mod_scene_finish_spawn(ctx, scene, name, entity_id, key, pos, rot, scale, func_idx);
  duk_push_int(ctx, handle);
  return 1;
}

static duk_ret_t bind_despawn(duk_context *ctx) {
  ModSceneCtx *scene = mod_scene_from_ctx(ctx);
  const int handle = duk_require_int(ctx, 0);
  NgSceneInst *inst = mod_scene_inst_from_handle(handle);
  if (!inst) {
    return 0;
  }
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
  if (inst->world_id != 0) {
    ng_world_despawn(mod_sim_world(), inst->world_id);
    inst->world_id = 0;
  }
#endif
  mod_scene_call_entity_method(scene, inst, "stop");
  mod_scene_call_entity_method(scene, inst, "dispose");
  // agent: composer-2.5 | 2026-07-29 | body shape fixed_step wire | 37245c
  mod_scene_physics_detach(handle);
  mod_scene_graph_despawn(handle);
  return 0;
}

static duk_ret_t bind_dispose(duk_context *ctx) {
  const char *kind = duk_require_string(ctx, 0);
  const char *name = duk_require_string(ctx, 1);
  mod_scene_assets_dispose(kind, name);
  mod_scene_physics_dispose(kind, name);
  mod_scene_graph_dispose_desc(kind, name);
  return 0;
}

static bool mod_scene_input_key_down(int key, int buttons) {
  if (key == NG_SCENE_KEY_A) {
    return (buttons & NG_INPUT_A) != 0;
  }
  if (key == NG_SCENE_KEY_D) {
    return (buttons & NG_INPUT_D) != 0;
  }
  if (key == NG_SCENE_KEY_W) {
    return (buttons & NG_INPUT_W) != 0;
  }
  if (key == NG_SCENE_KEY_S) {
    return (buttons & NG_INPUT_S) != 0;
  }
  if (key == NG_SCENE_KEY_F) {
    return (buttons & NG_INPUT_F) != 0;
  }
  return false;
}

/* OR of all peers' committed bits for step_tick (shared controls). */
// agent: composer-2.5 | 2026-08-01 | rename input get_local_any_peer | 827710
// agent: composer-2.5 | 2026-08-01 | lockstep bits for dedicated host | b1ef71
static duk_ret_t bind_get_any_input(duk_context *ctx) {
  const int key = duk_require_int(ctx, 0);
  int buttons = mod_input_buttons();
  if (mod_lockstep_active()) {
    const uint32_t tick = mod_lockstep_step_tick();
    /* Dedicated clock owner has local_peer_id=0 (no seat). Still must OR
     * remote peer slots — peer_count==1 used to fall through to empty kb. */
    if (tick != 0u) {
      buttons = (int)mod_lockstep_bits_or(tick);
    }
  }
  duk_push_boolean(ctx, mod_scene_input_key_down(key, buttons) ? 1 : 0);
  return 1;
}

/* Live keyboard/view sample — propose actions, not sim authority. */
static duk_ret_t bind_get_local_input(duk_context *ctx) {
  const int key = duk_require_int(ctx, 0);
  duk_push_boolean(ctx, mod_scene_input_key_down(key, mod_input_buttons()) ? 1 : 0);
  return 1;
}

/* Committed lockstep bits for one peer at step_tick; peer_id 0 → local peer. */
static duk_ret_t bind_get_peer_input(duk_context *ctx) {
  const int key = duk_require_int(ctx, 0);
  uint32_t peer_id = (uint32_t)duk_require_uint(ctx, 1);
  int buttons = 0;
  if (mod_lockstep_active()) {
    if (peer_id == 0) {
      peer_id = mod_lockstep_local_peer_id();
    }
    const uint32_t tick = mod_lockstep_step_tick();
    if (peer_id != 0 && mod_lockstep_have_input(peer_id, tick)) {
      buttons = (int)mod_lockstep_bits_for(peer_id, tick);
    }
  } else if (peer_id == 0 || peer_id == mod_lockstep_local_peer_id()) {
    buttons = mod_input_buttons();
  }
  duk_push_boolean(ctx, mod_scene_input_key_down(key, buttons) ? 1 : 0);
  return 1;
}

/* Deprecated alias → get_any_input. */
static duk_ret_t bind_get_input(duk_context *ctx) { return bind_get_any_input(ctx); }

// agent: composer-2.5 | 2026-07-30 | apply_impulse JS binding | b980ed
static duk_ret_t bind_apply_impulse(duk_context *ctx) {
  const int handle = duk_require_int(ctx, 0);
  float ix = 0.0f, iy = 0.0f, iz = 0.0f;
  if (duk_is_object(ctx, 1)) {
    duk_get_prop_string(ctx, 1, "x");
    ix = (float)duk_get_number(ctx, -1);
    duk_pop(ctx);
    duk_get_prop_string(ctx, 1, "y");
    iy = (float)duk_get_number(ctx, -1);
    duk_pop(ctx);
    duk_get_prop_string(ctx, 1, "z");
    iz = (float)duk_get_number(ctx, -1);
    duk_pop(ctx);
  } else {
    ix = (float)duk_get_number(ctx, 1);
    iy = (float)duk_get_number(ctx, 2);
    iz = (float)duk_get_number(ctx, 3);
  }
  duk_push_boolean(ctx, mod_scene_physics_apply_impulse(handle, ix, iy, iz) ? 1 : 0);
  return 1;
}

// agent: composer-2.5 | 2026-07-30 | apply force torque JS bindings | cd7a18
static bool mod_scene_bind_read_vec3(duk_context *ctx, int idx, float *x, float *y, float *z) {
  if (duk_is_object(ctx, idx)) {
    duk_get_prop_string(ctx, idx, "x");
    *x = (float)duk_get_number(ctx, -1);
    duk_pop(ctx);
    duk_get_prop_string(ctx, idx, "y");
    *y = (float)duk_get_number(ctx, -1);
    duk_pop(ctx);
    duk_get_prop_string(ctx, idx, "z");
    *z = (float)duk_get_number(ctx, -1);
    duk_pop(ctx);
    return true;
  }
  *x = (float)duk_get_number(ctx, idx);
  *y = (float)duk_get_number(ctx, idx + 1);
  *z = (float)duk_get_number(ctx, idx + 2);
  return true;
}

static duk_ret_t bind_apply_force(duk_context *ctx) {
  const int handle = duk_require_int(ctx, 0);
  float fx = 0.0f, fy = 0.0f, fz = 0.0f;
  mod_scene_bind_read_vec3(ctx, 1, &fx, &fy, &fz);
  duk_push_boolean(ctx, mod_scene_physics_apply_force(handle, fx, fy, fz) ? 1 : 0);
  return 1;
}

static duk_ret_t bind_apply_torque(duk_context *ctx) {
  const int handle = duk_require_int(ctx, 0);
  float tx = 0.0f, ty = 0.0f, tz = 0.0f;
  mod_scene_bind_read_vec3(ctx, 1, &tx, &ty, &tz);
  duk_push_boolean(ctx, mod_scene_physics_apply_torque(handle, tx, ty, tz) ? 1 : 0);
  return 1;
}

// agent: composer-2.5 | 2026-07-30 | gravity vel mass js api | f956eb
static duk_ret_t bind_set_linear_velocity(duk_context *ctx) {
  const int handle = duk_require_int(ctx, 0);
  float vx = 0.0f, vy = 0.0f, vz = 0.0f;
  mod_scene_bind_read_vec3(ctx, 1, &vx, &vy, &vz);
  duk_push_boolean(ctx, mod_scene_physics_set_linear_velocity(handle, vx, vy, vz) ? 1 : 0);
  return 1;
}

static duk_ret_t bind_get_linear_velocity(duk_context *ctx) {
  const int handle = duk_require_int(ctx, 0);
  float v[3] = {0.0f, 0.0f, 0.0f};
  (void)mod_scene_physics_get_linear_velocity(handle, v);
  duk_push_object(ctx);
  duk_push_number(ctx, v[0]);
  duk_put_prop_string(ctx, -2, "x");
  duk_push_number(ctx, v[1]);
  duk_put_prop_string(ctx, -2, "y");
  duk_push_number(ctx, v[2]);
  duk_put_prop_string(ctx, -2, "z");
  return 1;
}

static duk_ret_t bind_get_mass(duk_context *ctx) {
  const int handle = duk_require_int(ctx, 0);
  duk_push_number(ctx, mod_scene_physics_get_mass(handle));
  return 1;
}

// agent: codex-5.3 | 2026-07-29 | add mouse ray plane helper | 27b035
// agent: composer-2.5 | 2026-07-29 | shared raycast plane helper | 7c1d4a
bool mod_scene_raycast_plane_y(float plane_y, float *out_x, float *out_y, float *out_z) {
#if defined(NG_SERVER)
  (void)plane_y;
  (void)out_x;
  (void)out_y;
  (void)out_z;
  return false;
#else
  mod_scene_runtime_use_view();
  const NgSceneViewMeta *view = mod_scene_assets_view();
  Camera3D cam = {
      .position = {0.0f, 2.0f, 6.0f},
      .target = {0.0f, 0.0f, 0.0f},
      .up = {0.0f, 1.0f, 0.0f},
      .fovy = 45.0f,
      .projection = CAMERA_PERSPECTIVE,
  };
  if (view && view->valid) {
    cam.fovy = view->cam_fovy;
    cam.target = (Vector3){view->cam_target[0], view->cam_target[1], view->cam_target[2]};
    if (view->camera_mode == NG_SCENE_CAM_ORBIT) {
      const float t = (float)GetTime();
      const float radius = view->orbit_radius;
      cam.position.x = sinf(t * view->orbit_speed) * radius;
      cam.position.z = cosf(t * view->orbit_speed) * radius;
      cam.position.y = view->orbit_height + sinf(t * view->orbit_speed * 0.5f) * 0.5f;
    } else {
      cam.position = (Vector3){view->cam_pos[0], view->cam_pos[1], view->cam_pos[2]};
    }
  }
  float mx = 0.0f;
  float my = 0.0f;
  (void)mod_input_mouse_pos(&mx, &my);
  Ray ray = GetScreenToWorldRay((Vector2){mx, my}, cam);
  const float denom = ray.direction.y;
  if (fabsf(denom) < 0.0001f) {
    return false;
  }
  const float t = (plane_y - ray.position.y) / denom;
  if (t < 0.0f) {
    return false;
  }
  if (out_x) {
    *out_x = ray.position.x + ray.direction.x * t;
  }
  if (out_y) {
    *out_y = ray.position.y + ray.direction.y * t;
  }
  if (out_z) {
    *out_z = ray.position.z + ray.direction.z * t;
  }
  return true;
#endif
}

void mod_scene_raycast_plane_y_text(float plane_y, char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
#if defined(NG_SERVER)
  (void)plane_y;
  snprintf(out, cap, "hit=0 server");
#else
  float mx = 0.0f, my = 0.0f, hx = 0.0f, hy = 0.0f, hz = 0.0f;
  (void)mod_input_mouse_pos(&mx, &my);
  if (!mod_scene_raycast_plane_y(plane_y, &hx, &hy, &hz)) {
    snprintf(out, cap, "hit=0 mouse=%.1f,%.1f plane_y=%.3f", mx, my, plane_y);
    return;
  }
  snprintf(out, cap, "hit=1 mouse=%.1f,%.1f pos=%.3f,%.3f,%.3f", mx, my, hx, hy, hz);
#endif
}

static duk_ret_t bind_raycast_plane_y(duk_context *ctx) {
  const float plane_y = duk_is_number(ctx, 0) ? (float)duk_get_number(ctx, 0) : 0.0f;
  float hx = 0.0f, hy = 0.0f, hz = 0.0f;
  duk_push_object(ctx);
  if (!mod_scene_raycast_plane_y(plane_y, &hx, &hy, &hz)) {
    duk_push_false(ctx);
    duk_put_prop_string(ctx, -2, "hit");
    return 1;
  }
  duk_push_true(ctx);
  duk_put_prop_string(ctx, -2, "hit");
  duk_push_number(ctx, hx);
  duk_put_prop_string(ctx, -2, "x");
  duk_push_number(ctx, hy);
  duk_put_prop_string(ctx, -2, "y");
  duk_push_number(ctx, hz);
  duk_put_prop_string(ctx, -2, "z");
  return 1;
}

// agent: composer-2.5 | 2026-07-29 | expose JS mouse position | 8a4c2f
static duk_ret_t bind_get_mouse_pos(duk_context *ctx) {
  float mx = 0.0f, my = 0.0f;
  (void)mod_input_mouse_pos(&mx, &my);
  duk_push_object(ctx);
  duk_push_number(ctx, mx);
  duk_put_prop_string(ctx, -2, "x");
  duk_push_number(ctx, my);
  duk_put_prop_string(ctx, -2, "y");
  return 1;
}

static duk_ret_t bind_set_position(duk_context *ctx) {
  NgSceneInst *inst = mod_scene_inst_from_handle(duk_require_int(ctx, 0));
  if (!inst) {
    return 0;
  }
  float x = inst->pos[0], y = inst->pos[1], z = inst->pos[2];
  if (duk_is_object(ctx, 1)) {
    duk_get_prop_string(ctx, 1, "x");
    x = (float)duk_get_number(ctx, -1);
    duk_pop(ctx);
    duk_get_prop_string(ctx, 1, "y");
    y = (float)duk_get_number(ctx, -1);
    duk_pop(ctx);
    duk_get_prop_string(ctx, 1, "z");
    z = (float)duk_get_number(ctx, -1);
    duk_pop(ctx);
  }
  // agent: composer-2.5 | 2026-07-29 | position dirty deadband | 2c6e8a
  const float dx = x - inst->pos[0];
  const float dy = y - inst->pos[1];
  const float dz = z - inst->pos[2];
  if (dx * dx + dy * dy + dz * dz < 1.0e-8f) {
    return 0;
  }
  inst->pos[0] = x;
  inst->pos[1] = y;
  inst->pos[2] = z;
  mod_scene_graph_registry_set_pose(inst->id, inst->pos, inst->rot, inst->scale);
  if (ng_sync_posts_wire(inst->sync)) {
    mod_scene_graph_mark_dirty(inst, NG_COMP_POS);
  }
  return 0;
}

// agent: composer-2.5 | 2026-07-29 | js get_position binding | 9b4d7e
static duk_ret_t bind_get_position(duk_context *ctx) {
  NgSceneInst *inst = mod_scene_inst_from_handle(duk_require_int(ctx, 0));
  duk_push_object(ctx);
  if (!inst) {
    return 1;
  }
  duk_push_number(ctx, inst->pos[0]);
  duk_put_prop_string(ctx, -2, "x");
  duk_push_number(ctx, inst->pos[1]);
  duk_put_prop_string(ctx, -2, "y");
  duk_push_number(ctx, inst->pos[2]);
  duk_put_prop_string(ctx, -2, "z");
  return 1;
}

static duk_ret_t bind_set_rotation(duk_context *ctx) {
  NgSceneInst *inst = mod_scene_inst_from_handle(duk_require_int(ctx, 0));
  if (!inst) {
    return 0;
  }
  if (duk_is_object(ctx, 1)) {
    duk_get_prop_string(ctx, 1, "x");
    inst->rot[0] = (float)duk_get_number(ctx, -1);
    duk_pop(ctx);
    duk_get_prop_string(ctx, 1, "y");
    inst->rot[1] = (float)duk_get_number(ctx, -1);
    duk_pop(ctx);
    duk_get_prop_string(ctx, 1, "z");
    inst->rot[2] = (float)duk_get_number(ctx, -1);
    duk_pop(ctx);
  } else {
    inst->rot[0] = (float)duk_get_number(ctx, 1);
    inst->rot[1] = (float)duk_get_number(ctx, 2);
    inst->rot[2] = (float)duk_get_number(ctx, 3);
  }
  mod_scene_graph_registry_set_pose(inst->id, inst->pos, inst->rot, inst->scale);
  mod_scene_graph_mark_dirty(inst, NG_COMP_ROT);
  return 0;
}

static duk_ret_t bind_set_rotation_y(duk_context *ctx) {
  NgSceneInst *inst = mod_scene_inst_from_handle(duk_require_int(ctx, 0));
  if (!inst) {
    return 0;
  }
  inst->rot[1] = (float)duk_get_number(ctx, 1);
  mod_scene_graph_registry_set_pose(inst->id, inst->pos, inst->rot, inst->scale);
  mod_scene_graph_mark_dirty(inst, NG_COMP_ROT);
  return 0;
}

static duk_ret_t bind_get_rotation_y(duk_context *ctx) {
  NgSceneInst *inst = mod_scene_inst_from_handle(duk_require_int(ctx, 0));
  if (!inst) {
    duk_push_number(ctx, 0);
    return 1;
  }
  duk_push_number(ctx, inst->rot[1]);
  return 1;
}

static duk_ret_t bind_get_rotation(duk_context *ctx) {
  NgSceneInst *inst = mod_scene_inst_from_handle(duk_require_int(ctx, 0));
  duk_push_object(ctx);
  if (!inst) {
    return 1;
  }
  duk_push_number(ctx, inst->rot[0]);
  duk_put_prop_string(ctx, -2, "x");
  duk_push_number(ctx, inst->rot[1]);
  duk_put_prop_string(ctx, -2, "y");
  duk_push_number(ctx, inst->rot[2]);
  duk_put_prop_string(ctx, -2, "z");
  return 1;
}

static duk_ret_t bind_set_rotation_x(duk_context *ctx) {
  NgSceneInst *inst = mod_scene_inst_from_handle(duk_require_int(ctx, 0));
  if (!inst) {
    return 0;
  }
  inst->rot[0] = (float)duk_get_number(ctx, 1);
  mod_scene_graph_registry_set_pose(inst->id, inst->pos, inst->rot, inst->scale);
  mod_scene_graph_mark_dirty(inst, NG_COMP_ROT);
  return 0;
}

static duk_ret_t bind_get_rotation_x(duk_context *ctx) {
  NgSceneInst *inst = mod_scene_inst_from_handle(duk_require_int(ctx, 0));
  if (!inst) {
    duk_push_number(ctx, 0);
    return 1;
  }
  duk_push_number(ctx, inst->rot[0]);
  return 1;
}

static duk_ret_t bind_set_scale(duk_context *ctx) {
  NgSceneInst *inst = mod_scene_inst_from_handle(duk_require_int(ctx, 0));
  if (!inst) {
    return 0;
  }
  inst->scale = (float)duk_get_number(ctx, 1);
  mod_scene_graph_registry_set_pose(inst->id, inst->pos, inst->rot, inst->scale);
  if (ng_sync_posts_wire(inst->sync)) {
    mod_scene_graph_mark_dirty(inst, NG_COMP_SCALE);
  }
  return 0;
}

// agent: composer-2.5 | 2026-07-29 | js change scene binding and drain | d70ea8
static duk_ret_t bind_is_server(duk_context *ctx) {
  duk_push_boolean(ctx, mod_scene_is_server() ? 1 : 0);
  return 1;
}

static void mod_scene_clear_pending_module(duk_context *ctx) {
  if (!ctx) {
    return;
  }
  duk_push_global_stash(ctx);
  duk_del_prop_string(ctx, -1, NG_SCENE_PENDING_MODULE);
  duk_pop(ctx);
}

// agent: composer-2.5 | 2026-08-01 | js module wire host bindings | 884245
static duk_ret_t bind_module(duk_context *ctx) {
  if (!duk_is_function(ctx, 0)) {
    NG_LOG_ERROR("module: module() requires a constructor");
    return 0;
  }
  duk_push_global_stash(ctx);
  duk_get_prop_string(ctx, -1, NG_SCENE_PENDING_MODULE);
  if (duk_is_function(ctx, -1)) {
    NG_LOG_WARN("module: module() called twice in one file; last wins");
  }
  duk_pop(ctx);
  duk_dup(ctx, 0);
  duk_put_prop_string(ctx, -2, NG_SCENE_PENDING_MODULE);
  duk_pop(ctx);
  return 0;
}

static duk_ret_t bind_register(duk_context *ctx) {
  const char *id = duk_require_string(ctx, 0);
  const char *path = duk_require_string(ctx, 1);
  duk_push_boolean(ctx, ng_jsmod_register(id, path) ? 1 : 0);
  return 1;
}

static bool mod_scene_eval_js_file_mod(duk_context *ctx, const char *path);

static bool mod_scene_load_module_ctor(duk_context *ctx, const char *id, const char *path) {
  char ckey[80];
  snprintf(ckey, sizeof(ckey), "ng_modctor_%s", id);
  duk_push_global_stash(ctx);
  duk_get_prop_string(ctx, -1, ckey);
  if (duk_is_function(ctx, -1)) {
    duk_remove(ctx, -2);
    return true;
  }
  duk_pop(ctx);
  if (!mod_scene_eval_js_file_mod(ctx, path)) {
    duk_pop(ctx);
    return false;
  }
  duk_get_prop_string(ctx, -1, NG_SCENE_PENDING_MODULE);
  if (!duk_is_function(ctx, -1)) {
    NG_LOG_ERROR("module: file did not call module(): %s", path);
    duk_pop_n(ctx, 2);
    return false;
  }
  duk_dup(ctx, -1);
  duk_put_prop_string(ctx, -3, ckey);
  duk_remove(ctx, -2);
  mod_scene_clear_pending_module(ctx);
  return true;
}

static bool mod_scene_call_wired_one(ModSceneCtx *ctx, int idx, const char *method, int nargs);

static duk_ret_t bind_wire(duk_context *ctx) {
  ModSceneCtx *scene = mod_scene_from_ctx(ctx);
  const char *id = duk_require_string(ctx, 0);
  if (!scene || !id || id[0] == '\0') {
    return 0;
  }
  for (int i = 0; i < scene->wire_count; i++) {
    if (strcmp(scene->wire_ids[i], id) == 0) {
      NG_LOG_WARN("module: wire(%s) already attached; skip", id);
      return 0;
    }
  }
  if (scene->wire_count >= (int)(sizeof(scene->wire_ids) / sizeof(scene->wire_ids[0]))) {
    NG_LOG_ERROR("module: wire list full");
    return 0;
  }
  const char *path = ng_jsmod_lookup(id);
  if (!path) {
    NG_LOG_ERROR("module: wire unknown id=%s", id);
    return 0;
  }
  if (!mod_scene_load_module_ctor(ctx, id, path)) {
    return 0;
  }
  duk_new(ctx, 0);
  if (!duk_is_object(ctx, -1)) {
    NG_LOG_ERROR("module: wire instance failed id=%s", id);
    duk_pop(ctx);
    return 0;
  }
  const int idx = scene->wire_count;
  char wkey[48];
  snprintf(wkey, sizeof(wkey), "ng_wire_%d", idx);
  duk_push_global_stash(ctx);
  duk_dup(ctx, -2);
  duk_put_prop_string(ctx, -2, wkey);
  duk_pop(ctx);
  duk_pop(ctx);
  strncpy(scene->wire_ids[idx], id, sizeof(scene->wire_ids[idx]) - 1);
  scene->wire_ids[idx][sizeof(scene->wire_ids[idx]) - 1] = '\0';
  scene->wire_count++;
  (void)mod_scene_call_wired_one(scene, idx, "init", 0);
  NG_LOG_INFO("module: wire id=%s idx=%d", id, idx);
  return 0;
}

static duk_ret_t bind_get_view_camera(duk_context *ctx) {
  // agent: composer-2.5 | 2026-08-01 | view camera force view rt | f9c6af
  // agent: composer-2.5 | 2026-08-01 | restore runtime after view cam | dd716e
  /* Always read the view runtime camera; restore caller runtime afterward. */
  NgSceneRuntime *prev = mod_scene_runtime_active();
  mod_scene_runtime_use_view();
  const NgSceneViewMeta *view = mod_scene_assets_view();
  duk_push_object(ctx);
  if (view && view->valid) {
    duk_push_object(ctx);
    duk_push_number(ctx, view->cam_pos[0]);
    duk_put_prop_string(ctx, -2, "x");
    duk_push_number(ctx, view->cam_pos[1]);
    duk_put_prop_string(ctx, -2, "y");
    duk_push_number(ctx, view->cam_pos[2]);
    duk_put_prop_string(ctx, -2, "z");
    duk_put_prop_string(ctx, -2, "position");
    duk_push_object(ctx);
    duk_push_number(ctx, view->cam_target[0]);
    duk_put_prop_string(ctx, -2, "x");
    duk_push_number(ctx, view->cam_target[1]);
    duk_put_prop_string(ctx, -2, "y");
    duk_push_number(ctx, view->cam_target[2]);
    duk_put_prop_string(ctx, -2, "z");
    duk_put_prop_string(ctx, -2, "target");
  }
  if (prev == &g_scene_server) {
    mod_scene_runtime_use_server();
  } else {
    mod_scene_runtime_use_view();
  }
  return 1;
}

static duk_ret_t bind_change_scene(duk_context *ctx) {
  ModSceneCtx *scene = mod_scene_from_ctx(ctx);
  const char *scene_id = duk_require_string(ctx, 0);
  if (!scene || !scene_id || scene_id[0] == '\0' || !mod_scene_is_server()) {
    duk_push_false(ctx);
    return 1;
  }
  strncpy(scene->pending_scene_id, scene_id, sizeof(scene->pending_scene_id) - 1);
  scene->pending_scene_id[sizeof(scene->pending_scene_id) - 1] = '\0';
  duk_push_true(ctx);
  return 1;
}

// agent: composer-2.5 | 2026-08-01 | action_register action bindings | 234498
static int mod_scene_pack_action_args(duk_context *ctx, duk_idx_t arg0, float *argv, int maxn) {
  int n = 0;
  const duk_idx_t top = duk_get_top(ctx);
  for (duk_idx_t i = arg0; i < top && n < maxn; i++) {
    if (duk_is_number(ctx, i)) {
      argv[n++] = (float)duk_get_number(ctx, i);
      continue;
    }
    if (!duk_is_object(ctx, i)) {
      continue;
    }
    duk_get_prop_string(ctx, i, "x");
    if (duk_is_number(ctx, -1) && n < maxn) {
      argv[n++] = (float)duk_get_number(ctx, -1);
    }
    duk_pop(ctx);
    duk_get_prop_string(ctx, i, "y");
    if (duk_is_number(ctx, -1) && n < maxn) {
      argv[n++] = (float)duk_get_number(ctx, -1);
    }
    duk_pop(ctx);
    duk_get_prop_string(ctx, i, "z");
    if (duk_is_number(ctx, -1) && n < maxn) {
      argv[n++] = (float)duk_get_number(ctx, -1);
    }
    duk_pop(ctx);
  }
  return n;
}

static duk_ret_t bind_action_register(duk_context *ctx) {
  const char *name = duk_require_string(ctx, 1);
  duk_push_boolean(ctx, ng_jsact_register(ctx, 0, name) ? 1 : 0);
  return 1;
}

static duk_ret_t bind_action(duk_context *ctx) {
  // agent: composer-2.5 | 2026-08-01 | quiet tip-full propose warn | dd7b48
  const char *name = duk_require_string(ctx, 0);
  float argv[NG_LOCK_ACTION_FLOATS];
  const int argc = mod_scene_pack_action_args(ctx, 1, argv, NG_LOCK_ACTION_FLOATS);
  const uint16_t id = ng_jsact_hash_name(name);
  if (mod_lockstep_active()) {
    const bool ok = mod_lockstep_propose_local_action(id, (uint8_t)argc, argv);
    if (!ok) {
      NG_LOG_WARN("action: propose failed name=%s id=%u (no peer/tip)", name, (unsigned)id);
    }
    duk_push_boolean(ctx, ok ? 1 : 0);
    return 1;
  }
  duk_push_boolean(ctx, ng_jsact_call(ctx, name, (uint8_t)argc, argv) ? 1 : 0);
  return 1;
}

static duk_ret_t bind_action_peer(duk_context *ctx) {
  duk_push_number(ctx, (double)ng_jsact_apply_peer());
  return 1;
}

static duk_ret_t bind_action_tick(duk_context *ctx) {
  duk_push_number(ctx, (double)ng_jsact_apply_tick());
  return 1;
}

static void mod_scene_bind_global(duk_context *ctx) {
  duk_push_global_object(ctx);

#define BIND(name, fn, nargs)                                                                                          \
  duk_push_c_function(ctx, fn, nargs);                                                                                 \
  duk_put_prop_string(ctx, -2, name);

  BIND("describe", bind_describe, 3);
  BIND("spawn", bind_spawn, 2);
  BIND("despawn", bind_despawn, 1);
  BIND("dispose", bind_dispose, 2);
  BIND("get_any_input", bind_get_any_input, 1);
  BIND("get_local_input", bind_get_local_input, 1);
  BIND("get_peer_input", bind_get_peer_input, 2);
  BIND("get_input", bind_get_input, 1); /* alias → get_any_input */
  // agent: composer-2.5 | 2026-07-30 | apply_impulse JS binding | b980ed
  // agent: composer-2.5 | 2026-07-30 | apply force torque JS bindings | cd7a18
  BIND("apply_impulse", bind_apply_impulse, 4);
  BIND("apply_force", bind_apply_force, 4);
  BIND("apply_torque", bind_apply_torque, 4);
  // agent: composer-2.5 | 2026-07-30 | gravity vel mass js api | f956eb
  BIND("set_linear_velocity", bind_set_linear_velocity, 4);
  BIND("get_linear_velocity", bind_get_linear_velocity, 1);
  BIND("get_mass", bind_get_mass, 1);
  // agent: codex-5.3 | 2026-07-29 | bind scene mouse raycast fn | b831e0
  BIND("raycast_plane_y", bind_raycast_plane_y, 1);
  // agent: composer-2.5 | 2026-07-29 | expose JS mouse position | 8a4c2f
  BIND("get_mouse_pos", bind_get_mouse_pos, 0);
  BIND("set_position", bind_set_position, 2);
  // agent: composer-2.5 | 2026-07-29 | js get_position binding | 9b4d7e
  BIND("get_position", bind_get_position, 1);
  BIND("set_rotation", bind_set_rotation, DUK_VARARGS);
  BIND("set_rotation_y", bind_set_rotation_y, 2);
  BIND("set_rotation_x", bind_set_rotation_x, 2);
  BIND("get_rotation_y", bind_get_rotation_y, 1);
  BIND("get_rotation_x", bind_get_rotation_x, 1);
  BIND("get_rotation", bind_get_rotation, 1);
  BIND("set_scale", bind_set_scale, 2);
  BIND("is_server", bind_is_server, 0);
  BIND("change_scene", bind_change_scene, 1);
  // agent: composer-2.5 | 2026-08-01 | js module wire host bindings | 884245
  BIND("module", bind_module, 1);
  BIND("register", bind_register, 2);
  BIND("wire", bind_wire, 1);
  BIND("get_view_camera", bind_get_view_camera, 0);
  // agent: composer-2.5 | 2026-08-01 | action_register action bindings | 234498
  BIND("action_register", bind_action_register, 2);
  BIND("action", bind_action, DUK_VARARGS);
  BIND("action_peer", bind_action_peer, 0);
  BIND("action_tick", bind_action_tick, 0);

#undef BIND

  duk_push_int(ctx, NG_SCENE_KEY_A);
  duk_put_prop_string(ctx, -2, "KEY_A");
  duk_push_int(ctx, NG_SCENE_KEY_D);
  duk_put_prop_string(ctx, -2, "KEY_D");
  // agent: codex-5.3 | 2026-07-29 | export JS W S keycodes | 44cd8b
  duk_push_int(ctx, NG_SCENE_KEY_W);
  duk_put_prop_string(ctx, -2, "KEY_W");
  duk_push_int(ctx, NG_SCENE_KEY_S);
  duk_put_prop_string(ctx, -2, "KEY_S");
  // agent: composer-2.5 | 2026-08-01 | scene KEY_F constant | 58ea6c
  duk_push_int(ctx, NG_SCENE_KEY_F);
  duk_put_prop_string(ctx, -2, "KEY_F");

  duk_pop(ctx);
}

static bool mod_scene_eval_js_file(duk_context *ctx, const char *path) {
  if (!ctx || !path) {
    return false;
  }
  char *text = ng_fs_read_text(path);
  if (!text) {
    return false;
  }
  duk_push_string(ctx, text);
  const bool ok = (duk_peval(ctx) == DUK_EXEC_SUCCESS);
  if (!ok) {
    NG_LOG_ERROR("scene js error (%s): %s", path, duk_safe_to_string(ctx, -1));
  }
  duk_pop(ctx);
  ng_fs_free_text(text);
  return ok;
}

static bool mod_scene_eval_js_file_mod(duk_context *ctx, const char *path) {
  if (!ctx || !path) {
    return false;
  }
  if (g_jsmod_eval_depth >= 8) {
    NG_LOG_ERROR("module: eval nest depth exceeded (%s)", path);
    return false;
  }
  char prev[NG_JSMOD_PATH_MAX];
  const char *cur = ng_jsmod_current_file();
  if (cur) {
    strncpy(prev, cur, sizeof(prev) - 1);
    prev[sizeof(prev) - 1] = '\0';
  } else {
    prev[0] = '\0';
  }
  g_jsmod_eval_depth++;
  ng_jsmod_set_current_file(path);
  mod_scene_clear_pending_module(ctx);
  const bool ok = mod_scene_eval_js_file(ctx, path);
  g_jsmod_eval_depth--;
  ng_jsmod_set_current_file(prev[0] ? prev : NULL);
  return ok;
}

static bool mod_scene_load_helpers(duk_context *ctx) {
  char path[128];
  snprintf(path, sizeof(path), NG_RES_ROOT "helpers.js");
  if (!mod_scene_eval_js_file(ctx, path)) {
    NG_LOG_WARN("helpers.js missing: %s", path);
    return false;
  }
  return true;
}

static bool mod_scene_wants_helpers(const char *scene_id) {
  return scene_id && strcmp(scene_id, "example") == 0;
}

static void mod_scene_prepare_duk(ModSceneCtx *ctx) {
  mod_scene_bind_global(ctx->ctx);
  duk_push_global_object(ctx->ctx);
  duk_dup(ctx->ctx, -1);
  duk_put_global_string(ctx->ctx, "global");
  duk_pop(ctx->ctx);
}

static bool mod_scene_finish_js_load(ModSceneCtx *ctx) {
  duk_push_global_stash(ctx->ctx);
  duk_get_prop_string(ctx->ctx, -1, NG_SCENE_PENDING_MODULE);
  duk_remove(ctx->ctx, -2);
  if (!duk_is_function(ctx->ctx, -1)) {
    duk_pop(ctx->ctx);
    duk_get_global_string(ctx->ctx, "Scene");
    if (!duk_is_function(ctx->ctx, -1)) {
      NG_LOG_ERROR("module: no module() export and Scene constructor missing");
      duk_pop(ctx->ctx);
      return false;
    }
    NG_LOG_WARN("module: deprecated global Scene; use global.module(Ctor)");
  }
  duk_new(ctx->ctx, 0);
  if (!duk_is_object(ctx->ctx, -1)) {
    NG_LOG_ERROR("Scene instance create failed");
    duk_pop(ctx->ctx);
    return false;
  }
  duk_push_global_stash(ctx->ctx);
  duk_insert(ctx->ctx, -2);
  duk_put_prop_string(ctx->ctx, -2, NG_SCENE_STASH_KEY);
  duk_pop(ctx->ctx);
  mod_scene_clear_pending_module(ctx->ctx);
  ctx->scene_inst_stash = 1;
  return true;
}

static bool mod_scene_load_js_path(ModSceneCtx *ctx, const char *path, bool with_helpers) {
  mod_scene_prepare_duk(ctx);
  if (with_helpers && !mod_scene_load_helpers(ctx->ctx)) {
    return false;
  }
  if (!mod_scene_eval_js_file_mod(ctx->ctx, path)) {
    return false;
  }
  return mod_scene_finish_js_load(ctx);
}

static bool mod_scene_load_js(ModSceneCtx *ctx, const char *scene_id) {
  if (!ctx->ctx || !scene_id) {
    return false;
  }
  char path[NG_JSMOD_PATH_MAX];
  if (strcmp(scene_id, "boot") == 0) {
    // agent: composer-2.5 | 2026-07-28 | boot js outside scenes dir | b4o5o6
    snprintf(path, sizeof(path), NG_RES_ROOT "boot.js");
    return mod_scene_load_js_path(ctx, path, false);
  }
  const char *reg = ng_jsmod_lookup(scene_id);
  if (reg) {
    const bool helpers = mod_scene_wants_helpers(scene_id);
    if (mod_scene_load_js_path(ctx, reg, helpers)) {
      return true;
    }
    NG_LOG_WARN("module: catalog path failed id=%s path=%s; trying filesystem", scene_id, reg);
  } else {
    NG_LOG_WARN("module: unregistered scene id=%s; filesystem fallback", scene_id);
  }
  const bool helpers = mod_scene_wants_helpers(scene_id);
  snprintf(path, sizeof(path), NG_RES_ROOT "scenes/%s.js", scene_id);
  if (mod_scene_load_js_path(ctx, path, helpers)) {
    return true;
  }
  if (!mod_scene_load_helpers(ctx->ctx)) {
    return false;
  }
  snprintf(path, sizeof(path), "tests/scenes/%s.js", scene_id);
  if (mod_scene_load_js_path(ctx, path, false)) {
    return true;
  }
  NG_LOG_ERROR("scene js missing: %s", scene_id);
  return false;
}

static bool mod_scene_call_wired_one(ModSceneCtx *ctx, int idx, const char *method, int nargs) {
  if (!ctx || !ctx->ctx || idx < 0 || idx >= ctx->wire_count || !method) {
    if (nargs > 0 && ctx && ctx->ctx) {
      duk_pop_n(ctx->ctx, nargs);
    }
    return false;
  }
  char wkey[48];
  snprintf(wkey, sizeof(wkey), "ng_wire_%d", idx);
  duk_push_global_stash(ctx->ctx);
  duk_get_prop_string(ctx->ctx, -1, wkey);
  duk_remove(ctx->ctx, -2);
  if (!duk_is_object(ctx->ctx, -1)) {
    duk_pop(ctx->ctx);
    if (nargs > 0) {
      duk_pop_n(ctx->ctx, nargs);
    }
    return false;
  }
  duk_get_prop_string(ctx->ctx, -1, method);
  if (!duk_is_function(ctx->ctx, -1)) {
    duk_pop_n(ctx->ctx, 2);
    if (nargs > 0) {
      duk_pop_n(ctx->ctx, nargs);
    }
    return false;
  }
  if (nargs > 0) {
    duk_insert(ctx->ctx, -(nargs + 2));
    duk_insert(ctx->ctx, -(nargs + 1));
  } else {
    duk_insert(ctx->ctx, -2);
  }
  if (duk_pcall_method(ctx->ctx, nargs) != DUK_EXEC_SUCCESS) {
    NG_LOG_ERROR("module.%s.%s: %s", ctx->wire_ids[idx], method, duk_safe_to_string(ctx->ctx, -1));
    duk_pop(ctx->ctx);
    return false;
  }
  duk_pop(ctx->ctx);
  return true;
}

static void mod_scene_call_all_wired(ModSceneCtx *ctx, const char *method, bool reverse) {
  if (!ctx || ctx->wire_count <= 0 || !method) {
    return;
  }
  if (reverse) {
    for (int i = ctx->wire_count - 1; i >= 0; i--) {
      (void)mod_scene_call_wired_one(ctx, i, method, 0);
    }
  } else {
    for (int i = 0; i < ctx->wire_count; i++) {
      (void)mod_scene_call_wired_one(ctx, i, method, 0);
    }
  }
}

static void mod_scene_call_all_wired_dt(ModSceneCtx *ctx, const char *method, float dt) {
  if (!ctx || !ctx->ctx || ctx->wire_count <= 0 || !method) {
    return;
  }
  for (int i = 0; i < ctx->wire_count; i++) {
    duk_push_number(ctx->ctx, dt);
    (void)mod_scene_call_wired_one(ctx, i, method, 1);
  }
}

static bool mod_scene_call_method(ModSceneCtx *ctx, const char *method, int nargs) {
  if (!ctx->ctx) {
    return false;
  }
  duk_push_global_stash(ctx->ctx);
  duk_get_prop_string(ctx->ctx, -1, NG_SCENE_STASH_KEY);
  duk_remove(ctx->ctx, -2);
  if (!duk_is_object(ctx->ctx, -1)) {
    duk_pop(ctx->ctx);
    if (nargs > 0) {
      duk_pop_n(ctx->ctx, nargs);
    }
    return false;
  }
  duk_get_prop_string(ctx->ctx, -1, method);
  if (!duk_is_function(ctx->ctx, -1)) {
    duk_pop_n(ctx->ctx, 2);
    // agent: composer-2.5 | 2026-07-29 | body shape fixed_step wire | 37245c
    if (nargs > 0) {
      duk_pop_n(ctx->ctx, nargs);
    }
    return false;
  }
  if (nargs > 0) {
    duk_insert(ctx->ctx, -(nargs + 2));
    duk_insert(ctx->ctx, -(nargs + 1));
  } else {
    duk_insert(ctx->ctx, -2);
  }
  if (duk_pcall_method(ctx->ctx, nargs) != DUK_EXEC_SUCCESS) {
    NG_LOG_ERROR("scene.%s: %s", method, duk_safe_to_string(ctx->ctx, -1));
    duk_pop(ctx->ctx);
    return false;
  }
  duk_pop(ctx->ctx);
  return true;
}

static bool mod_scene_call_start(ModSceneCtx *ctx) {
  // agent: composer-2.5 | 2026-08-01 | spawn context sim id guards | 7c6221
  const NgSpawnCtx prev = mod_scene_spawn_get_ctx();
  mod_scene_spawn_set_ctx(NG_SPAWN_CTX_START);
  const bool ok = mod_scene_call_method(ctx, "start", 1);
  /* Wired module start (empty for sample-shooting) while still in START. */
  mod_scene_call_all_wired(ctx, "start", false);
  mod_scene_spawn_set_ctx(prev);
  return ok;
}

static void mod_scene_drain_pending_change(ModSceneCtx *ctx) {
  if (!ctx || !ctx->pending_scene_id[0] || !mod_scene_is_server()) {
    return;
  }
  char next_scene[32];
  strncpy(next_scene, ctx->pending_scene_id, sizeof(next_scene) - 1);
  next_scene[sizeof(next_scene) - 1] = '\0';
  ctx->pending_scene_id[0] = '\0';
  /* Same id must reload — solar→solar restarts the scene (not a no-op). */
  // agent: cursor-grok-4.5 | 2026-07-31 | allow same scene reload | 4ee66e
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
  char reply[256];
  if (mod_sim_load_scene) {
    mod_sim_load_scene(next_scene, reply, sizeof(reply));
  }
#endif
}

static void mod_scene_call_lifecycle(ModSceneCtx *ctx, const char *method) {
  const bool teardown = method && (strcmp(method, "stop") == 0 || strcmp(method, "dispose") == 0);
  if (teardown) {
    mod_scene_call_all_wired(ctx, method, true);
    mod_scene_call_method(ctx, method, 0);
  } else {
    mod_scene_call_method(ctx, method, 0);
    /* init: wire() already calls module init; do not double-invoke. */
    if (method && strcmp(method, "init") != 0) {
      mod_scene_call_all_wired(ctx, method, false);
    }
  }
  mod_scene_drain_pending_change(ctx);
}

static void mod_scene_push_session_obj(ModSceneCtx *ctx, const NgSessionState *session) {
  duk_push_object(ctx->ctx);
  duk_push_string(ctx->ctx, session->scene_id);
  duk_put_prop_string(ctx->ctx, -2, "scene_id");
  duk_push_boolean(ctx->ctx, ctx->is_controller ? 1 : 0);
  duk_put_prop_string(ctx->ctx, -2, "is_controller");
  duk_push_int(ctx->ctx, session->controller_id);
  duk_put_prop_string(ctx->ctx, -2, "controller_id");
  duk_push_int(ctx->ctx, session->your_id);
  duk_put_prop_string(ctx->ctx, -2, "your_id");
  // agent: composer-2.5 | 2026-07-30 | gravity vel mass js api | f956eb
  duk_push_boolean(ctx->ctx, session->syncing ? 1 : 0);
  duk_put_prop_string(ctx->ctx, -2, "syncing");
  duk_push_uint(ctx->ctx, session->snap_tick);
  duk_put_prop_string(ctx->ctx, -2, "snap_tick");
  duk_push_boolean(ctx->ctx, session->lockstep ? 1 : 0);
  duk_put_prop_string(ctx->ctx, -2, "lockstep");
}

static void mod_scene_materialize_pending_ud(const NgSessionSpawn *sp, void *ud) {
  ModSceneCtx *ctx = (ModSceneCtx *)ud;
  if (!ctx || !sp) {
    return;
  }
  if (mod_scene_graph_inst_by_id(sp->entity_id)) {
    return;
  }
  // agent: composer-2.5 | 2026-07-30 | lockstep ignore entity sync | ae273c
  if (sp->key[0] != '\0' && mod_scene_graph_inst_by_key(sp->key)) {
    return;
  }
  const bool on_server = mod_scene_is_server();
  NgSceneDesc *desc = mod_scene_graph_entity_desc(sp->desc_name);
  const bool lockstep_body = mod_scene_lockstep_body(desc ? desc->body : NULL);
  if (!(mod_scene_spawn_creates_local(sp->sync, on_server, ctx->is_controller, lockstep_body) ||
        mod_scene_spawn_should_materialize(sp->sync, on_server, lockstep_body))) {
    mod_scene_graph_registry_add_instance(sp->desc_name, sp->key, sp->entity_id, sp->sync, sp->pos,
                                          sp->rot, sp->scale > 0.0f ? sp->scale : 1.0f);
    return;
  }
  NG_LOG_WARN("spawn pending unmatched desc=%s key=%s id=%u", sp->desc_name, sp->key,
              sp->entity_id);
  const int func_idx = mod_scene_stash_func(ctx->ctx, sp->desc_name);
  (void)mod_scene_spawn_from_pending(ctx->ctx, ctx, sp->desc_name, sp, func_idx);
}

static void mod_scene_pull_entity_phase(ModSceneCtx *ctx, NgSceneInst *inst) {
  if (!ctx->ctx || !inst) {
    return;
  }
  char key[48];
  snprintf(key, sizeof(key), "inst_%d", inst->handle);
  duk_push_global_stash(ctx->ctx);
  duk_get_prop_string(ctx->ctx, -1, key);
  if (!duk_is_object(ctx->ctx, -1)) {
    duk_pop_n(ctx->ctx, 2);
    return;
  }
  duk_get_prop_string(ctx->ctx, -1, "phase");
  if (duk_is_number(ctx->ctx, -1)) {
    inst->phase = (float)duk_get_number(ctx->ctx, -1);
  }
  duk_pop_n(ctx->ctx, 3);
}

static void mod_scene_run_entity_steps(ModSceneCtx *ctx, float dt) {
  if (!ctx->ctx) {
    return;
  }
  const bool on_server = mod_scene_is_server();
  const int n = mod_scene_graph_inst_count();
  for (int i = 0; i < n; i++) {
    const NgSceneInst *inst = mod_scene_graph_inst_at(i);
    if (!inst) {
      continue;
    }
    if (on_server) {
      if (inst->sync != NG_SYNC_SERVER) {
        continue;
      }
    } else {
      if (inst->sync == NG_SYNC_SERVER) {
        continue;
      }
      if (inst->sync == NG_SYNC_OWNER && !ctx->is_controller) {
        continue;
      }
    }
    char key[48];
    snprintf(key, sizeof(key), "inst_%d", inst->handle);
    duk_push_global_stash(ctx->ctx);
    duk_get_prop_string(ctx->ctx, -1, key);
    if (!duk_is_object(ctx->ctx, -1)) {
      duk_pop_n(ctx->ctx, 2);
      continue;
    }
    duk_get_prop_string(ctx->ctx, -1, "step");
    if (!duk_is_function(ctx->ctx, -1)) {
      duk_pop_n(ctx->ctx, 3);
      continue;
    }
    duk_insert(ctx->ctx, -2);
    duk_push_number(ctx->ctx, dt);
    // agent: composer-2.5 | 2026-07-30 | pop stash after entity step | e12822
    if (duk_pcall_method(ctx->ctx, 1) != DUK_EXEC_SUCCESS) {
      NG_LOG_ERROR("entity step: %s", duk_safe_to_string(ctx->ctx, -1));
    }
    duk_pop(ctx->ctx); /* result/error */
    duk_pop(ctx->ctx); /* stash */
    mod_scene_drain_pending_change(ctx);
    if (on_server && inst->sync == NG_SYNC_SERVER) {
      mod_scene_pull_entity_phase(ctx, (NgSceneInst *)inst);
    }
  }
}

static void mod_scene_set_active_for(ModSceneCtx *ctx);

// agent: composer-2.5 | 2026-07-29 | body shape fixed_step wire | 37245c
static void mod_scene_run_entity_fixed_steps(ModSceneCtx *ctx, float fixed_dt) {
  if (!ctx->ctx) {
    return;
  }
  // agent: composer-2.5 | 2026-07-30 | fixed_step body vs sync | 5afc64
  // agent: composer-2.5 | 2026-07-30 | skip view phys step lockstep | 890ea4
  const bool on_server = mod_scene_is_server();
  /* In-process: view skips duplicate body scripts when server owns the world.
   * Each network peer still runs input→force→step on its phys owner. */
  const bool lockstep_view_skip =
      mod_scene_physics_is_input_sim() && ctx == &g_scene_view.scene &&
      g_scene_server.scene.loaded &&
      (g_scene_server.physics.sim_mode == NG_PHYS_SIM_LOCKSTEP ||
       g_scene_server.physics.sim_mode == NG_PHYS_SIM_HYBRID);
  const int n = mod_scene_graph_inst_count();
  for (int i = 0; i < n; i++) {
    const NgSceneInst *inst = mod_scene_graph_inst_at(i);
    if (!inst) {
      continue;
    }
    /* Lockstep bodies: run on phys owner only (avoid double torque in one process). */
    if (mod_scene_lockstep_body(inst->body)) {
      if (lockstep_view_skip) {
        continue;
      }
    } else {
      /* Bodiless follow entity.sync (cube rules). */
      if (on_server) {
        if (inst->sync != NG_SYNC_SERVER) {
          continue;
        }
      } else {
        if (inst->sync == NG_SYNC_SERVER) {
          continue;
        }
        if (inst->sync == NG_SYNC_OWNER && !ctx->is_controller) {
          continue;
        }
      }
    }
    char key[48];
    snprintf(key, sizeof(key), "inst_%d", inst->handle);
    duk_push_global_stash(ctx->ctx);
    duk_get_prop_string(ctx->ctx, -1, key);
    if (!duk_is_object(ctx->ctx, -1)) {
      duk_pop_n(ctx->ctx, 2);
      continue;
    }
    duk_get_prop_string(ctx->ctx, -1, "fixed_step");
    if (!duk_is_function(ctx->ctx, -1)) {
      duk_pop_n(ctx->ctx, 3);
      continue;
    }
    duk_insert(ctx->ctx, -2);
    duk_push_number(ctx->ctx, fixed_dt);
    // agent: composer-2.5 | 2026-07-30 | pop stash after fixed_step | 1a5c26
    if (duk_pcall_method(ctx->ctx, 1) != DUK_EXEC_SUCCESS) {
      NG_LOG_ERROR("entity fixed_step: %s", duk_safe_to_string(ctx->ctx, -1));
    }
    duk_pop(ctx->ctx); /* result/error */
    duk_pop(ctx->ctx); /* stash */
    mod_scene_drain_pending_change(ctx);
  }
}

static void mod_scene_fixed_step_ctx(ModSceneCtx *ctx, float fixed_dt) {
  if (!ctx || !ctx->loaded || !ctx->started || ctx->native || !ctx->ctx) {
    return;
  }
  // agent: composer-2.5 | 2026-07-30 | skip view phys step lockstep | 890ea4
  // agent: composer-2.5 | 2026-07-30 | guard duk stack on scene tick | 10fe96
  // agent: composer-2.5 | 2026-08-01 | action_register action bindings | 234498
  mod_scene_set_active_for(ctx);
  const duk_idx_t stack_top = duk_get_top(ctx->ctx);
  const bool lockstep_view_skip =
      mod_scene_physics_is_input_sim() && ctx == &g_scene_view.scene &&
      g_scene_server.scene.loaded &&
      (g_scene_server.physics.sim_mode == NG_PHYS_SIM_LOCKSTEP ||
       g_scene_server.physics.sim_mode == NG_PHYS_SIM_HYBRID);
  /* Dispatch on every loaded heap so view graph spawns for draw; phys attach
   * still skipped on view when server owns the world. */
  if (mod_lockstep_active()) {
    ng_jsact_dispatch_tick(ctx->ctx, mod_lockstep_step_tick());
  }
  duk_push_number(ctx->ctx, fixed_dt);
  mod_scene_call_method(ctx, "fixed_step", 1);
  mod_scene_call_all_wired_dt(ctx, "fixed_step", fixed_dt);
  mod_scene_run_entity_fixed_steps(ctx, fixed_dt);
  duk_set_top(ctx->ctx, stack_top);
  if (!lockstep_view_skip) {
    mod_scene_physics_fixed_step(fixed_dt, mod_scene_is_server(), ctx->is_controller);
  }
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
  // agent: composer-2.5 | 2026-07-30 | apply_remote skip lockstep bodies | 7b8dd8
  if (mod_scene_is_server()) {
    mod_scene_mirror_server(mod_sim_world());
  }
#endif
}

// agent: composer-2.5 | 2026-07-29 | server phys poses to view | c05110
// agent: composer-2.5 | 2026-07-29 | server phys snapshot to view | d2d78d
// agent: composer-2.5 | 2026-07-30 | push lockstep poses to view | 25fe35
#if !defined(NG_SERVER)
static void mod_scene_push_server_phys_to_view(void) {
  typedef struct {
    uint32_t id;
    char key[32];
    float pos[3];
    float rot[3];
  } NgPhysPose;
  NgPhysPose poses[NG_SCENE_INST_MAX];
  int n = 0;
  const bool lockstep = mod_scene_physics_is_input_sim() ||
                        g_scene_server.physics.sim_mode == NG_PHYS_SIM_LOCKSTEP ||
                        g_scene_server.physics.sim_mode == NG_PHYS_SIM_HYBRID;
  mod_scene_runtime_use_server();
  const int count = mod_scene_graph_inst_count();
  for (int i = 0; i < count && n < NG_SCENE_INST_MAX; i++) {
    const NgSceneInst *inst = mod_scene_graph_inst_at(i);
    if (!inst) {
      continue;
    }
    /* sim:server — sync:server bodies; lockstep — any local body with Box3D. */
    if (lockstep) {
      if (inst->body_id_bits == 0 && inst->body[0] == '\0') {
        continue;
      }
      if (inst->body_id_bits == 0) {
        continue;
      }
    } else if (inst->sync != NG_SYNC_SERVER || inst->body_id_bits == 0) {
      continue;
    }
    poses[n].id = inst->id;
    strncpy(poses[n].key, inst->key, sizeof(poses[n].key) - 1);
    poses[n].key[sizeof(poses[n].key) - 1] = '\0';
    poses[n].pos[0] = inst->pos[0];
    poses[n].pos[1] = inst->pos[1];
    poses[n].pos[2] = inst->pos[2];
    poses[n].rot[0] = inst->rot[0];
    poses[n].rot[1] = inst->rot[1];
    poses[n].rot[2] = inst->rot[2];
    n++;
  }
  if (n == 0) {
    return;
  }
  mod_scene_runtime_use_view();
  if (!NG_SCENE_ACTIVE()->loaded) {
    return;
  }
  for (int i = 0; i < n; i++) {
    // agent: composer-2.5 | 2026-08-01 | push by id after sim ids | 61401b
    // agent: composer-2.5 | 2026-08-01 | push prefer key then id | 15a52d
    /* Keyed start entities: key survives per-heap alloc_id skew. Keyless
     * action balls: sim-band id is the only stable handle. */
    NgSceneInst *v = NULL;
    if (poses[i].key[0] != '\0') {
      v = mod_scene_graph_inst_by_key(poses[i].key);
    }
    if (!v) {
      v = mod_scene_graph_inst_by_id(poses[i].id);
    }
    if (!v) {
      continue;
    }
    v->pos[0] = poses[i].pos[0];
    v->pos[1] = poses[i].pos[1];
    v->pos[2] = poses[i].pos[2];
    v->rot[0] = poses[i].rot[0];
    v->rot[1] = poses[i].rot[1];
    v->rot[2] = poses[i].rot[2];
  }
}
#endif

static void mod_scene_fixed_step(void *vctx, float fixed_dt, uint32_t tick) {
  (void)vctx;
  (void)tick;
  if (mod_lockstep_active()) {
    mod_lockstep_set_step_tick(mod_lockstep_sim_tick() + 1u);
  }
  mod_scene_fixed_step_ctx(&g_scene_server.scene, fixed_dt);
  mod_scene_fixed_step_ctx(&g_scene_view.scene, fixed_dt);
#if !defined(NG_SERVER)
  // agent: composer-2.5 | 2026-07-29 | lockstep scene sim flag | b3f626
  // agent: composer-2.5 | 2026-07-30 | push lockstep poses to view | 25fe35
  // agent: composer-2.5 | 2026-08-01 | push poses after both heaps | e810ca
  /* After both heaps dispatch actions (view spawn exists), copy server poses → view. */
  if (!mod_scene_physics_is_input_sim() ||
      (g_scene_server.scene.loaded &&
       (g_scene_server.physics.sim_mode == NG_PHYS_SIM_LOCKSTEP ||
        g_scene_server.physics.sim_mode == NG_PHYS_SIM_HYBRID))) {
    mod_scene_push_server_phys_to_view();
  }
#endif
  if (mod_lockstep_active()) {
    // agent: composer-2.5 | 2026-07-30 | lockstep hash tick only | ca6913
    // agent: composer-2.5 | 2026-07-30 | lockstep own sim clock | 5e56e9
    uint32_t hash = 0;
    const uint32_t next = mod_lockstep_sim_tick() + 1u;
    mod_scene_runtime_use_server();
    if (!g_scene_server.scene.loaded ||
        (g_scene_server.physics.sim_mode != NG_PHYS_SIM_LOCKSTEP &&
         g_scene_server.physics.sim_mode != NG_PHYS_SIM_HYBRID)) {
      mod_scene_runtime_use_view();
    }
    if (mod_scene_physics_is_input_sim() && next > 0 && (next % 60u) == 0u) {
      hash = mod_scene_physics_checksum();
    }
    mod_lockstep_on_stepped(next, hash);
  }
  mod_scene_runtime_use_server();
}

// agent: composer-2.5 | 2026-07-31 | lockstep resim after confirm | bdb5d7
void mod_scene_lockstep_pump_resim(void) {
  if (!mod_lockstep_active()) {
    return;
  }
  uint32_t target = mod_lockstep_resim_to();
  if (target == 0u) {
    return;
  }
  if (mod_lockstep_sim_tick() >= target) {
    mod_lockstep_clear_resim();
    return;
  }
  /* Cap like Gaffer spiral — slightly above normal frame catch-up for rollback. */
  const int cap = NG_MOD_FIXED_MAX_STEPS * 2;
  int steps = 0;
  while (mod_lockstep_sim_tick() < target && steps < cap) {
    if (mod_lockstep_gate() != NG_LOCK_GATE_GO) {
      break;
    }
    mod_scene_fixed_step(NULL, NG_MOD_FIXED_DT, mod_lockstep_sim_tick() + 1u);
    steps++;
  }
  if (mod_lockstep_sim_tick() >= target) {
    mod_lockstep_clear_resim();
  }
}

static void mod_scene_set_active_for(ModSceneCtx *ctx) {
  if (ctx == &g_scene_server.scene) {
    mod_scene_runtime_use_server();
  } else if (ctx == &g_scene_view.scene) {
    mod_scene_runtime_use_view();
  }
}

static void mod_scene_unload(ModSceneCtx *ctx) {
  if (!ctx) {
    return;
  }
  mod_scene_set_active_for(ctx);
  if (ctx->native) {
    mod_scene_native_unload();
  } else if (ctx->ctx && ctx->started) {
    mod_scene_call_lifecycle(ctx, "stop");
    mod_scene_call_lifecycle(ctx, "dispose");
  }
  if (ctx->ctx) {
    // agent: composer-2.5 | 2026-08-01 | action_register action bindings | 234498
    ng_jsact_clear(ctx->ctx);
    duk_destroy_heap(ctx->ctx);
    ctx->ctx = NULL;
  }
  mod_scene_graph_reset();
  mod_scene_assets_reset();
  // agent: composer-2.5 | 2026-07-29 | lockstep scene sim flag | b3f626
  // agent: composer-2.5 | 2026-07-30 | unload always clears lockstep gate | 314d8e
  {
    const bool was_lock =
        mod_scene_physics_is_input_sim() || (mod_lockstep_active() && ctx->loaded);
    const bool other_lock =
        (ctx == &g_scene_server.scene)
            ? (g_scene_view.physics.sim_mode == NG_PHYS_SIM_LOCKSTEP ||
               g_scene_view.physics.sim_mode == NG_PHYS_SIM_HYBRID)
            : (g_scene_server.physics.sim_mode == NG_PHYS_SIM_LOCKSTEP ||
               g_scene_server.physics.sim_mode == NG_PHYS_SIM_HYBRID);
    mod_scene_physics_reset();
    /* Drop the gate whenever this was the last lockstep runtime — otherwise cube
     * (lockstep=0) keeps STALLing after solar/physics. */
    if (was_lock && !other_lock) {
      mod_lockstep_reset();
      ng_mod_set_fixed_gate(NULL);
    }
  }
  ctx->loaded = false;
  ctx->inited = false;
  ctx->started = false;
  ctx->native = false;
  ctx->is_controller = false;
  ctx->is_server_host = false;
  ctx->scene_id[0] = '\0';
  ctx->wire_count = 0;
  memset(ctx->wire_ids, 0, sizeof(ctx->wire_ids));
}

void mod_scene_clear_lockstep_server(void) {
  // agent: composer-2.5 | 2026-07-30 | unload always clears lockstep gate | 314d8e
  mod_scene_runtime_use_server();
  ModSceneCtx *ctx = mod_scene_runtime_scene();
  if (ctx && ctx->loaded) {
    mod_scene_unload(ctx);
  }
  if (mod_lockstep_active()) {
    mod_lockstep_reset();
    ng_mod_set_fixed_gate(NULL);
  }
}

static bool mod_scene_begin(const char *scene_id, bool server_host, bool is_controller) {
  ModSceneCtx *ctx = mod_scene_runtime_scene();
  mod_scene_unload(ctx);
  strncpy(ctx->scene_id, scene_id, sizeof(ctx->scene_id) - 1);
  ctx->is_server_host = server_host;
  ctx->is_controller = is_controller;
  ctx->wire_count = 0;
  memset(ctx->wire_ids, 0, sizeof(ctx->wire_ids));
  mod_scene_graph_reset();
  mod_scene_assets_reset();
  // agent: composer-2.5 | 2026-07-29 | body shape fixed_step wire | 37245c
  mod_scene_physics_reset();
  ctx->ctx = duk_create_heap_default();
  if (!ctx->ctx) {
    return false;
  }
  if (mod_scene_load_js(ctx, scene_id)) {
    ctx->loaded = true;
    mod_scene_call_lifecycle(ctx, "init");
    NG_LOG_INFO("module: scene=%s wires=%d", ctx->scene_id, ctx->wire_count);
    ctx->inited = true;
    return true;
  }
  duk_destroy_heap(ctx->ctx);
  ctx->ctx = NULL;
  // agent: composer-2.5 | 2026-07-28 | wire native scene fallback path | 096c5c
  if (mod_scene_native_load(scene_id)) {
    ctx->native = true;
    ctx->loaded = true;
    ctx->inited = true;
    return true;
  }
  mod_scene_unload(ctx);
  return false;
}

bool mod_scene_load(const char *scene_id) {
  // agent: composer-2.5 | 2026-07-30 | lockstep ignore entity sync | ae273c
#if !defined(NG_SERVER)
  /* Drop view graph before server reload so second scene load cannot leave duplicates. */
  mod_scene_runtime_use_view();
  if (mod_scene_runtime_scene()->loaded) {
    mod_scene_unload(mod_scene_runtime_scene());
  }
#endif
  mod_scene_runtime_use_server();
  if (!scene_id || scene_id[0] == '\0') {
    mod_scene_unload(mod_scene_runtime_scene());
    return true;
  }
  ModSceneCtx *ctx = mod_scene_runtime_scene();
  if (!mod_scene_begin(scene_id, true, true)) {
    return false;
  }
  if (ctx->native) {
    ctx->started = true;
    return true;
  }
  NgSessionState session = {0};
  strncpy(session.scene_id, scene_id, sizeof(session.scene_id) - 1);
  // agent: composer-2.5 | 2026-08-01 | session mode apply fill | 4dc3e3
  session.lockstep = mod_scene_physics_is_input_sim() ? (uint8_t)mod_scene_physics_sim_mode() : 0u;
  mod_scene_push_session_obj(ctx, &session);
  if (!mod_scene_call_start(ctx)) {
    mod_scene_unload(ctx);
    return false;
  }
  mod_scene_drain_pending_change(ctx);
  ctx->started = true;
  // agent: composer-2.5 | 2026-07-29 | lockstep scene sim flag | b3f626
  // agent: composer-2.5 | 2026-07-30 | lockstep restart on load | 0de823
  mod_scene_lockstep_maybe_restart(0);
  return true;
}

bool mod_scene_load_boot(void) { return mod_scene_load("boot"); }

bool mod_scene_is_loaded(void) {
  mod_scene_runtime_use_server();
  return NG_SCENE_ACTIVE()->loaded;
}

const char *mod_scene_current_id(void) {
  mod_scene_runtime_use_server();
  return NG_SCENE_ACTIVE()->scene_id;
}

int mod_scene_entity_count(void) {
  mod_scene_runtime_use_server();
  return mod_scene_graph_inst_count();
}

void mod_scene_fill_session(NgSessionState *session) {
  if (!session) {
    return;
  }
  mod_scene_runtime_use_server();
  // agent: composer-2.5 | 2026-07-29 | lockstep scene sim flag | b3f626
  // agent: composer-2.5 | 2026-08-01 | session mode apply fill | 4dc3e3
  session->lockstep = mod_scene_physics_is_input_sim() ? (uint8_t)mod_scene_physics_sim_mode() : 0u;
  mod_scene_graph_fill_session_spawns(session);
}

bool mod_scene_is_controller(void) {
  mod_scene_runtime_use_server();
  return NG_SCENE_ACTIVE()->is_controller;
}

bool mod_scene_can_author(NgSyncMode sync) {
  // agent: composer-2.5 | 2026-07-29 | author uses active runtime | b3f7a1
  // agent: composer-2.5 | 2026-07-30 | narrow can_author lockstep bodies | 5153b8
  /* Lockstep only owns physics bodies; bodiless shared/owner still author transforms. */
  if (sync == NG_SYNC_SHARED) {
    return true;
  }
  if (sync == NG_SYNC_OWNER) {
    return NG_SCENE_ACTIVE()->is_controller;
  }
  return false;
}

static void mod_scene_on_session_ctx(ModSceneCtx *ctx, const NgSessionState *session,
                                     bool force_reload) {
  // agent: composer-2.5 | 2026-07-29 | force reload on view session | 8b87bd
  mod_scene_set_active_for(ctx);
  if (!session) {
    return;
  }

  if (!ctx->loaded || strcmp(ctx->scene_id, session->scene_id) != 0 || force_reload) {
    // agent: composer-2.5 | 2026-07-30 | sphere describe parse | 741115
    /* Server runtime owns lockstep Box3D; view is display-only under lockstep. */
    const bool server_host = (ctx == &g_scene_server.scene);
    if (!mod_scene_begin(session->scene_id, server_host,
                         session->your_id != 0 && session->your_id == session->controller_id)) {
      return;
    }
    ctx->started = false;
  } else {
    ctx->is_controller =
        (session->your_id != 0 && session->your_id == session->controller_id);
  }

  if (ctx->native) {
    if (!ctx->started) {
      ctx->started = true;
    }
#ifndef NG_SERVER
    mod_render_apply_session(session);
#endif
    return;
  }

  // Seed pending matches for Scene.start spawn() call-order / keys.
  if (!ctx->started) {
    // agent: composer-2.5 | 2026-07-29 | lockstep scene sim flag | b3f626
    // agent: composer-2.5 | 2026-07-30 | host session syncing join | c76f26
    // agent: composer-2.5 | 2026-07-30 | lockstep join fixes logging | 486633
    // agent: composer-2.5 | 2026-08-01 | session mode apply fill | 4dc3e3
    if (session->lockstep == 1u) {
      mod_scene_physics_set_sim_mode(NG_PHYS_SIM_LOCKSTEP);
    } else if (session->lockstep == 2u) {
      mod_scene_physics_set_sim_mode(NG_PHYS_SIM_HYBRID);
    }
    /* Late-join only when host marks syncing — snap_tick alone false-triggers
     * await_phys STALL on ordinary scene switches. */
    // agent: cursor-grok-4.5 | 2026-07-31 | join_sync requires syncing flag | 73e30a
    const bool join_sync = session->lockstep && session->syncing;
    NG_LOG_INFO("lockstep: session start scene=%s your=%u ctrl=%u lock=%u syncing=%u snap=%u "
                "join_sync=%d",
                session->scene_id, session->your_id, session->controller_id, session->lockstep,
                session->syncing, session->snap_tick, join_sync ? 1 : 0);
    if (join_sync) {
      mod_lockstep_await_phys(true);
      mod_lockstep_begin_sync(session->snap_tick);
    }
    mod_scene_graph_seed_pending(session);
    mod_scene_push_session_obj(ctx, session);
    (void)mod_scene_call_start(ctx);
    mod_scene_drain_pending_change(ctx);
    ctx->started = true;
    {
      const NgSpawnCtx prev = mod_scene_spawn_get_ctx();
      mod_scene_spawn_set_ctx(NG_SPAWN_CTX_JOIN_MATERIALIZE);
      mod_scene_graph_foreach_unmatched_pending(mod_scene_materialize_pending_ud, ctx);
      mod_scene_spawn_set_ctx(prev);
    }
    // agent: composer-2.5 | 2026-07-30 | unload always clears lockstep gate | 314d8e
    // agent: composer-2.5 | 2026-07-30 | view lockstep activate not restart | 7a6732
    // agent: cursor-grok-4.5 | 2026-07-31 | view restart on scene change | abd7ca
    /* Fresh lockstep clock on scene begin. Late-join keeps snap_tick via
     * begin_sync after activate (do not wipe mid-join with a second restart). */
    if (join_sync) {
      mod_scene_lockstep_maybe_activate(session->your_id);
      if (mod_lockstep_active()) {
        mod_lockstep_begin_sync(session->snap_tick);
        mod_lockstep_await_phys(true);
        NG_LOG_INFO("lockstep: joiner waiting for phys snap tick=%u", session->snap_tick);
      }
    } else if (ctx == &g_scene_server.scene) {
      mod_scene_lockstep_maybe_restart(session->your_id);
    } else {
      /* View cold load: align to tick 0 when server slot did not restart this
       * process (pure view / scene switch). Keep-alive if already at 0. */
      if (mod_lockstep_active() && mod_lockstep_sim_tick() == 0u) {
        mod_scene_lockstep_maybe_activate(session->your_id);
      } else {
        mod_scene_lockstep_maybe_restart(session->your_id);
      }
    }
  } else if ((session->lockstep || mod_scene_physics_is_input_sim()) &&
             !mod_lockstep_active()) {
    // agent: composer-2.5 | 2026-07-30 | lockstep activate no wipe | bb830d
    mod_scene_lockstep_maybe_activate(session->your_id);
  }

#ifndef NG_SERVER
  mod_render_apply_session(session);
#endif
}

void mod_scene_on_session(const NgSessionState *session) {
  mod_scene_runtime_use_server();
  mod_scene_on_session_ctx(mod_scene_runtime_scene(), session, false);
}

void mod_scene_on_session_forced(const NgSessionState *session) {
  // agent: cursor-grok-4.5 | 2026-07-31 | scene epoch forces reload | 0ca291
  mod_scene_runtime_use_server();
  mod_scene_on_session_ctx(mod_scene_runtime_scene(), session, true);
}

void mod_scene_view_on_session(const NgSessionState *session) {
  mod_scene_runtime_use_view();
  mod_scene_on_session_ctx(mod_scene_runtime_scene(), session, true);
}

static bool mod_scene_update_is_lockstep_body(const NgStateUpdate *update) {
  // agent: composer-2.5 | 2026-07-30 | apply_remote skip lockstep bodies | 7b8dd8
  // agent: composer-2.5 | 2026-08-01 | input sim family checks | 1297a4
  if (!update || !mod_scene_physics_is_input_sim()) {
    return false;
  }
  NgSceneInst *inst = mod_scene_graph_inst_by_id(update->entity_id);
  return inst && mod_scene_lockstep_body(inst->body);
}

void mod_scene_apply_remote(const NgStateUpdate *update) {
  mod_scene_runtime_use_server();
  if (!update || !NG_SCENE_ACTIVE()->loaded) {
    return;
  }
  // agent: composer-2.5 | 2026-07-30 | apply_remote skip lockstep bodies | 7b8dd8
  if (mod_scene_update_is_lockstep_body(update)) {
    return;
  }
  mod_scene_graph_apply_update(update);
}

void mod_scene_view_apply_remote(const NgStateUpdate *update) {
  mod_scene_runtime_use_view();
  if (!update || !NG_SCENE_ACTIVE()->loaded) {
    return;
  }
  // agent: composer-2.5 | 2026-07-30 | apply_remote skip lockstep bodies | 7b8dd8
  if (mod_scene_update_is_lockstep_body(update)) {
    return;
  }
  ModSceneCtx *ctx = NG_SCENE_ACTIVE();
  NgSceneInst *inst = mod_scene_graph_inst_by_id(update->entity_id);
  // agent: composer-2.5 | 2026-08-01 | owner reconcile skip small err | ea4522
  bool skip_apply = false;
  if (inst && inst->sync == NG_SYNC_OWNER && ctx->is_controller && !inst->phys_proxy &&
      (update->comp_mask & NG_COMP_FLAGS) == 0) {
    float pos_err = 0.0f;
    float rot_err = 0.0f;
    if (update->comp_mask & NG_COMP_POS) {
      const float dx = inst->pos[0] - update->pos[0];
      const float dy = inst->pos[1] - update->pos[1];
      const float dz = inst->pos[2] - update->pos[2];
      pos_err = sqrtf(dx * dx + dy * dy + dz * dz);
    }
    if (update->comp_mask & NG_COMP_ROT) {
      rot_err = fabsf(inst->rot[0] - update->rot[0]);
      if (fabsf(inst->rot[1] - update->rot[1]) > rot_err) {
        rot_err = fabsf(inst->rot[1] - update->rot[1]);
      }
      if (fabsf(inst->rot[2] - update->rot[2]) > rot_err) {
        rot_err = fabsf(inst->rot[2] - update->rot[2]);
      }
    }
    if (pos_err < 0.05f && rot_err < 0.05f) {
      skip_apply = true;
    }
  }
  if (!skip_apply) {
    mod_scene_graph_apply_update(update);
  }
#if !defined(NG_SERVER)
  // agent: composer-2.5 | 2026-07-30 | view apply push sample drive | 62a9c2
  inst = mod_scene_graph_inst_by_id(update->entity_id);
  if (inst) {
    const double now = GetTime();
    mod_scene_graph_push_sample(inst, now);
    mod_scene_graph_note_state_arrival(now);
    if (inst->phys_proxy) {
      mod_scene_physics_drive_proxy(inst->handle, inst->pos, inst->rot, inst->lin_vel,
                                    inst->ang_vel);
    }
  }
#endif
}

// agent: composer-2.5 | 2026-08-01 | interest origin helper | 79d287
bool mod_scene_interest_origin(float out[3]) {
  if (!out) {
    return false;
  }
#if !defined(NG_SERVER)
  mod_scene_runtime_use_view();
  ModSceneCtx *vctx = NG_SCENE_ACTIVE();
  if (vctx && vctx->loaded && vctx->is_controller) {
    const int vn = mod_scene_graph_inst_count();
    for (int i = 0; i < vn; i++) {
      const NgSceneInst *inst = mod_scene_graph_inst_at(i);
      if (inst && inst->sync == NG_SYNC_OWNER && inst->body[0] != '\0') {
        out[0] = inst->pos[0];
        out[1] = inst->pos[1];
        out[2] = inst->pos[2];
        return true;
      }
    }
  }
#endif
  mod_scene_runtime_use_server();
  const int sn = mod_scene_graph_inst_count();
  for (int i = 0; i < sn; i++) {
    const NgSceneInst *inst = mod_scene_graph_inst_at(i);
    if (inst && inst->sync == NG_SYNC_SERVER && inst->body[0] != '\0') {
      out[0] = inst->pos[0];
      out[1] = inst->pos[1];
      out[2] = inst->pos[2];
      return true;
    }
  }
  return false;
}

// agent: composer-2.5 | 2026-07-29 | flush shared from view graph | c8e4f0
static bool mod_scene_take_flush_active(NgStateUpdate *out) {
  if (!out || !NG_SCENE_ACTIVE()->loaded) {
    return false;
  }
  // agent: composer-2.5 | 2026-07-30 | take_flush lockstep bodies only | bf6a08
  NgStateUpdate tmp;
  while (mod_scene_graph_take_dirty(&tmp)) {
    NgSceneInst *inst = mod_scene_graph_inst_by_id(tmp.entity_id);
    if (inst && mod_scene_lockstep_body(inst->body)) {
      continue;
    }
    if (inst && mod_scene_can_author(inst->sync)) {
      *out = tmp;
      return true;
    }
    if (inst && inst->sync == NG_SYNC_SERVER && mod_scene_is_server()) {
      *out = tmp;
      return true;
    }
  }
  return false;
}

bool mod_scene_take_flush(NgStateUpdate *out) {
#if defined(NG_SERVER)
  mod_scene_runtime_use_server();
  return mod_scene_take_flush_active(out);
#else
  // Shared/owner transforms are authored on the view graph.
  // agent: composer-2.5 | 2026-07-30 | shared apply ignores stale seq | 975e95
  mod_scene_runtime_use_view();
  if (mod_scene_take_flush_active(out)) {
    return true;
  }
  /* Remote clients must not drain boot/server graph ids onto the wire — they
   * collide with view shared entities (both often allocate from id=1). */
  if (mod_net_upstream_connected()) {
    return false;
  }
  mod_scene_runtime_use_server();
  return mod_scene_take_flush_active(out);
#endif
}

static bool mod_scene_tick_ctx(ModSceneCtx *ctx, float dt) {
  if (!ctx || !ctx->loaded || !ctx->started) {
    return true;
  }
  mod_scene_set_active_for(ctx);

  if (ctx->native) {
    mod_scene_native_step(dt);
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
    if (mod_scene_is_server()) {
      mod_scene_mirror_server(mod_sim_world());
    }
#endif
    return true;
  }

  if (!ctx->ctx) {
    return true;
  }

  // agent: composer-2.5 | 2026-07-30 | guard duk stack on scene tick | 10fe96
  const duk_idx_t stack_top = duk_get_top(ctx->ctx);
  mod_input_begin_frame();
  duk_push_number(ctx->ctx, dt);
  mod_scene_call_method(ctx, "step", 1);
  mod_scene_call_all_wired_dt(ctx, "step", dt);
  mod_scene_run_entity_steps(ctx, dt);
  duk_set_top(ctx->ctx, stack_top);
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
  if (mod_scene_is_server()) {
    mod_scene_mirror_server(mod_sim_world());
  }
#endif
  return true;
}

static bool mod_scene_on_msg(const NgMsg *msg, void *vctx) {
  (void)vctx;
  if (!msg || msg->kind != NG_MSG_TICK || msg->to != NG_BUS_ANY) {
    return false;
  }
  mod_scene_tick_ctx(&g_scene_server.scene, msg->dt);
  mod_scene_tick_ctx(&g_scene_view.scene, msg->dt);
  // agent: composer-2.5 | 2026-07-29 | tick wired inputs once | 5a2d9c
  mod_input_tick_wire();
  mod_scene_runtime_use_server();
  mod_net_flush_scene_updates();
  return true;
}

static bool mod_scene_init(void *vctx) {
  (void)vctx;
  mod_scene_runtime_use_server();
  memset(&g_scene_server, 0, sizeof(g_scene_server));
  mod_scene_graph_reset();
  mod_scene_runtime_use_view();
  memset(&g_scene_view, 0, sizeof(g_scene_view));
  mod_scene_graph_reset();
  mod_scene_runtime_use_server();
  return true;
}

static void mod_scene_shutdown(void *vctx) {
  (void)vctx;
  mod_scene_unload(&g_scene_view.scene);
  mod_scene_unload(&g_scene_server.scene);
  mod_scene_runtime_use_server();
}

static const NgModOps g_scene_ops = {
    .name = "scene",
    .dest = NG_BUS_SCENE,
    .side = NG_MOD_SIDE_BOTH,
    .init = mod_scene_init,
    .shutdown = mod_scene_shutdown,
    .on_msg = mod_scene_on_msg,
    .fixed_step = mod_scene_fixed_step,
};

const NgModOps *mod_scene_ops(void) { return &g_scene_ops; }

void *mod_scene_ctx(void) { return &g_scene_server.scene; }

bool mod_scene_graph_active(void) {
  mod_scene_runtime_use_server();
  return mod_scene_graph_inst_count() > 0;
}

bool mod_scene_view_is_loaded(void) {
  mod_scene_runtime_use_view();
  return NG_SCENE_ACTIVE()->loaded;
}

const char *mod_scene_view_current_id(void) {
  mod_scene_runtime_use_view();
  return NG_SCENE_ACTIVE()->scene_id;
}

int mod_scene_view_entity_count(void) {
  mod_scene_runtime_use_view();
  return mod_scene_graph_inst_count();
}

void mod_scene_view_status_text(char *out, size_t cap) {
  // agent: composer-2.5 | 2026-07-29 | view status text helper | d2e790
  if (!out || cap == 0) {
    return;
  }
  mod_scene_runtime_use_view();
  const char *id = NG_SCENE_ACTIVE()->scene_id;
  snprintf(out, cap, "view scene=%s loaded=%d graph=%d entities=%d", id && id[0] ? id : "",
           NG_SCENE_ACTIVE()->loaded ? 1 : 0, mod_scene_graph_inst_count(),
           mod_scene_graph_inst_count());
}

// agent: composer-2.5 | 2026-07-29 | view entity transform observe | 4d8e21
void mod_scene_view_entities_text(char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  mod_scene_runtime_use_view();
  size_t used = 0;
  const int n = mod_scene_graph_inst_count();
  used += (size_t)snprintf(out + used, cap > used ? cap - used : 0, "entities=%d", n);
  for (int i = 0; i < n && used + 1 < cap; i++) {
    const NgSceneInst *inst = mod_scene_graph_inst_at(i);
    if (!inst) {
      continue;
    }
    used += (size_t)snprintf(
        out + used, cap - used,
        " | id=%u desc=%s pos=%.3f,%.3f,%.3f rot=%.3f,%.3f,%.3f scale=%.3f", inst->id,
        inst->desc_name, inst->pos[0], inst->pos[1], inst->pos[2], inst->rot[0], inst->rot[1],
        inst->rot[2], inst->scale);
  }
  if (used == 0 && cap > 0) {
    out[0] = '\0';
  }
}

// agent: composer-2.5 | 2026-07-30 | server entities text helper | 529fd4
static void mod_scene_entities_text_active(char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  size_t used = 0;
  const int n = mod_scene_graph_inst_count();
  used += (size_t)snprintf(out + used, cap > used ? cap - used : 0, "entities=%d", n);
  for (int i = 0; i < n && used + 1 < cap; i++) {
    const NgSceneInst *inst = mod_scene_graph_inst_at(i);
    if (!inst) {
      continue;
    }
    float mass = 0.0f;
    float lv[3] = {0};
    if (inst->body_id_bits != 0) {
      mass = mod_scene_physics_get_mass(inst->handle);
      (void)mod_scene_physics_get_linear_velocity(inst->handle, lv);
    }
    used += (size_t)snprintf(
        out + used, cap - used,
        " | id=%u key=%s desc=%s body=%u proxy=%d mass=%.3f vel=%.3f,%.3f,%.3f pos=%.3f,%.3f,%.3f "
        "rot=%.3f,%.3f,%.3f",
        inst->id, inst->key, inst->desc_name, inst->body_id_bits != 0 ? 1u : 0u,
        inst->phys_proxy ? 1 : 0, mass, lv[0], lv[1], lv[2], inst->pos[0], inst->pos[1],
        inst->pos[2], inst->rot[0], inst->rot[1], inst->rot[2]);
  }
  if (used == 0 && cap > 0) {
    out[0] = '\0';
  }
}

void mod_scene_server_entities_text(char *out, size_t cap) {
  mod_scene_runtime_use_server();
  mod_scene_entities_text_active(out, cap);
}

void mod_scene_phys_debug_text(char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  char server[700];
  char view[700];
  mod_scene_server_entities_text(server, sizeof(server));
  mod_scene_runtime_use_view();
  mod_scene_entities_text_active(view, sizeof(view));
  int scripts = 0;
  int has_fs = 0;
  mod_scene_runtime_use_server();
  if (g_scene_server.scene.ctx) {
    duk_context *dctx = g_scene_server.scene.ctx;
    const int n = mod_scene_graph_inst_count();
    for (int i = 0; i < n; i++) {
      const NgSceneInst *inst = mod_scene_graph_inst_at(i);
      if (!inst) {
        continue;
      }
      char key[48];
      snprintf(key, sizeof(key), "inst_%d", inst->handle);
      duk_push_global_stash(dctx);
      duk_get_prop_string(dctx, -1, key);
      if (duk_is_object(dctx, -1)) {
        scripts++;
        duk_get_prop_string(dctx, -1, "fixed_step");
        if (duk_is_function(dctx, -1)) {
          has_fs++;
        }
        duk_pop(dctx);
      }
      duk_pop_n(dctx, 2);
    }
  }
  snprintf(out, cap,
           "buttons=%d step_tick=%u peers=%d lock=%d srv_sim=%d view_sim=%d "
           "scripts=%d fixed_step_fn=%d | server[%s] | view[%s]",
           mod_input_buttons(), mod_lockstep_step_tick(), mod_lockstep_peer_count(),
           mod_lockstep_active() ? 1 : 0, (int)g_scene_server.physics.sim_mode,
           (int)g_scene_view.physics.sim_mode, scripts, has_fs, server, view);
}

bool mod_scene_debug_apply_torque_key(const char *key, float tx, float ty, float tz) {
  if (!key || key[0] == '\0') {
    return false;
  }
  /* Prefer phys owner: server when it owns lockstep, else view. */
  if (g_scene_server.scene.loaded &&
      (g_scene_server.physics.sim_mode == NG_PHYS_SIM_LOCKSTEP ||
       g_scene_server.physics.sim_mode == NG_PHYS_SIM_HYBRID)) {
    mod_scene_runtime_use_server();
  } else {
    mod_scene_runtime_use_view();
  }
  NgSceneInst *inst = mod_scene_graph_inst_by_key(key);
  if (!inst) {
    return false;
  }
  return mod_scene_physics_apply_torque(inst->handle, tx, ty, tz);
}

bool mod_scene_view_graph_active(void) {
  mod_scene_runtime_use_view();
  return mod_scene_graph_inst_count() > 0;
}

void mod_scene_mirror_server(NgWorld *w) {
  mod_scene_runtime_use_server();
  if (!w || !NG_SCENE_ACTIVE()->loaded) {
    return;
  }
  // agent: composer-2.5 | 2026-07-30 | apply_remote skip lockstep bodies | 7b8dd8
  const int n = mod_scene_graph_inst_count();
  for (int i = 0; i < n; i++) {
    const NgSceneInst *cinst = mod_scene_graph_inst_at(i);
    if (!cinst) {
      continue;
    }
    NgSceneInst *inst = mod_scene_graph_inst_by_handle(cinst->handle);
    if (!inst || inst->sync != NG_SYNC_SERVER) {
      continue;
    }
    /* Lockstep physics bodies stay off the transform mirror path. */
    if (mod_scene_lockstep_body(inst->body)) {
      continue;
    }
    NgSceneResolvedModel resolved;
    NgEntityType type = NG_ENTITY_SPHERE;
    if (mod_scene_assets_resolve_model(inst->model, &resolved) && resolved.ok) {
      type = mod_scene_assets_entity_type_for_kind(resolved.mesh_kind);
    }
    if (inst->world_id == 0) {
      inst->world_id =
          ng_world_spawn(w, type, inst->pos[0], inst->pos[1], inst->pos[2]);
      if (inst->world_id != 0) {
        ng_world_set_public_id(w, inst->world_id, inst->id);
      }
    }
    if (inst->world_id != 0) {
      ng_world_set_entity_state(w, inst->world_id, inst->pos[0], inst->pos[1], inst->pos[2],
                                inst->rot[1], inst->phase);
    }
  }
}

void mod_scene_apply_snapshot(const NgSnapshot *snap) {
  mod_scene_runtime_use_server();
  if (!snap || !NG_SCENE_ACTIVE()->loaded) {
    return;
  }
  for (int i = 0; i < snap->entity_count; i++) {
    const NgEntitySnap *e = &snap->entities[i];
    NgSceneInst *inst = mod_scene_graph_inst_by_id(e->id);
    if (!inst || inst->sync != NG_SYNC_SERVER) {
      continue;
    }
    inst->pos[0] = e->pos[0];
    inst->pos[1] = e->pos[1];
    inst->pos[2] = e->pos[2];
    inst->rot[1] = e->rot_y;
    inst->phase = e->phase;
  }
}

void mod_scene_view_apply_snapshot(const NgSnapshot *snap) {
  mod_scene_runtime_use_view();
  if (!snap || !NG_SCENE_ACTIVE()->loaded) {
    return;
  }
  for (int i = 0; i < snap->entity_count; i++) {
    const NgEntitySnap *e = &snap->entities[i];
    NgSceneInst *inst = mod_scene_graph_inst_by_id(e->id);
    if (!inst || inst->sync != NG_SYNC_SERVER) {
      continue;
    }
    inst->pos[0] = e->pos[0];
    inst->pos[1] = e->pos[1];
    inst->pos[2] = e->pos[2];
    inst->rot[1] = e->rot_y;
    inst->phase = e->phase;
  }
}

// agent: composer-2.5 | 2026-07-27 | smoke cube registry sphere inst | c5f796
static bool mod_scene_smoke_registry_has_desc(const char *spawn_desc) {
  NgSessionState session = {0};
  mod_scene_graph_fill_session_spawns(&session);
  for (int i = 0; i < session.spawn_count; i++) {
    if (strcmp(session.spawns[i].desc_name, spawn_desc) == 0) {
      return true;
    }
  }
  for (int i = 0; i < mod_scene_graph_inst_count(); i++) {
    const NgSceneInst *inst = mod_scene_graph_inst_at(i);
    if (inst && strcmp(inst->desc_name, spawn_desc) == 0) {
      return true;
    }
  }
  return false;
}

static bool mod_scene_smoke_one(const char *scene_id, const char *spawn_desc, bool expect_inst) {
  mod_scene_runtime_use_server();
  if (!mod_scene_begin(scene_id, true, true)) {
    return false;
  }
  ModSceneCtx *ctx = mod_scene_runtime_scene();
  NgSessionState session = {0};
  strncpy(session.scene_id, scene_id, sizeof(session.scene_id) - 1);
  mod_scene_push_session_obj(ctx, &session);
  if (!mod_scene_call_start(ctx)) {
    mod_scene_unload(ctx);
    return false;
  }
  mod_scene_drain_pending_change(ctx);
  ctx->started = true;
  bool ok = true;
  if (expect_inst) {
    ok = mod_scene_graph_inst_count() >= 1;
  } else if (spawn_desc) {
    ok = mod_scene_smoke_registry_has_desc(spawn_desc);
  }
  mod_scene_unload(ctx);
  return ok;
}

bool mod_scene_is_native(void) {
  mod_scene_runtime_use_server();
  return NG_SCENE_ACTIVE()->native;
}

// agent: composer-2.5 | 2026-07-29 | lockstep smoke hash test | 258f24
// agent: composer-2.5 | 2026-07-30 | smoke single world input bits | 373a09
static bool mod_scene_lockstep_hash_smoke(void) {
  mod_lockstep_reset();
  ng_mod_set_fixed_gate(NULL);

  mod_scene_runtime_use_server();
  if (!mod_scene_begin("physics", true, true)) {
    return false;
  }
  {
    ModSceneCtx *ctx = mod_scene_runtime_scene();
    NgSessionState session = {0};
    strncpy(session.scene_id, "physics", sizeof(session.scene_id) - 1);
    session.lockstep = 2; /* hybrid */
    mod_scene_push_session_obj(ctx, &session);
    if (!mod_scene_call_start(ctx)) {
      mod_scene_unload(ctx);
      return false;
    }
    mod_scene_drain_pending_change(ctx);
    ctx->started = true;
    mod_scene_physics_set_sim_mode(NG_PHYS_SIM_HYBRID);
  }

#if !defined(NG_SERVER)
  mod_scene_runtime_use_view();
  if (!mod_scene_begin("physics", false, true)) {
    mod_scene_runtime_use_server();
    mod_scene_unload(mod_scene_runtime_scene());
    return false;
  }
  {
    ModSceneCtx *ctx = mod_scene_runtime_scene();
    NgSessionState session = {0};
    strncpy(session.scene_id, "physics", sizeof(session.scene_id) - 1);
    session.lockstep = 2;
    session.your_id = 1;
    session.controller_id = 1;
    mod_scene_graph_seed_pending(&session);
    mod_scene_push_session_obj(ctx, &session);
    if (!mod_scene_call_start(ctx)) {
      mod_scene_unload(ctx);
      mod_scene_runtime_use_server();
      mod_scene_unload(mod_scene_runtime_scene());
      return false;
    }
    mod_scene_drain_pending_change(ctx);
    ctx->started = true;
    mod_scene_physics_set_sim_mode(NG_PHYS_SIM_HYBRID);
  }
  /* View must not own a second Box3D world when server slot is lockstep. */
  {
    NgSceneInst *vbox = mod_scene_graph_inst_by_key("box");
    if (vbox && vbox->body_id_bits != 0) {
      mod_scene_unload(mod_scene_runtime_scene());
      mod_scene_runtime_use_server();
      mod_scene_unload(mod_scene_runtime_scene());
      return false;
    }
  }
#endif

  mod_lockstep_reset();
  mod_scene_lockstep_restart(0);
  // agent: composer-2.5 | 2026-07-30 | set lockstep clock owner | d24ebb
  /* Smoke exercises input slots; dedicated-server activate uses peer 0 (no player). */
  mod_lockstep_set_local_peer(1);
  mod_lockstep_set_clock_owner(true);
  mod_lockstep_note_roster();

  // agent: composer-2.5 | 2026-07-30 | smoke next sim tick bits | e7550b
  /* Gate must commit bits for sim_tick+1; step_tick must match. */
  {
    const NgLockGate g0 = mod_lockstep_gate();
    if (g0 == NG_LOCK_GATE_STALL) {
      mod_scene_runtime_use_server();
      mod_scene_unload(mod_scene_runtime_scene());
      return false;
    }
    const uint32_t next = mod_lockstep_sim_tick() + 1u;
    mod_lockstep_set_step_tick(next);
    if (!mod_lockstep_have_input(mod_lockstep_local_peer_id(), next)) {
      mod_scene_runtime_use_server();
      mod_scene_unload(mod_scene_runtime_scene());
      return false;
    }
  }

  /* Capture non-zero bits into a later slot (store_remote overwrites after gen_local). */
  mod_lockstep_set_clock_owner(false);
  mod_lockstep_store_remote_input(mod_lockstep_local_peer_id(), 2u, (uint8_t)(NG_INPUT_A | NG_INPUT_W),
                                  NULL);
  if (mod_lockstep_bits_for(mod_lockstep_local_peer_id(), 2u) != (uint8_t)(NG_INPUT_A | NG_INPUT_W)) {
    mod_scene_runtime_use_server();
    mod_scene_unload(mod_scene_runtime_scene());
    return false;
  }
  mod_lockstep_set_clock_owner(true);
  mod_lockstep_note_roster();

  bool ok = true;
  uint32_t sim_tick = 0;
  for (int i = 0; i < NG_LOCK_PLAYOUT_TICKS + 180 && ok; i++) {
    const NgLockGate g = mod_lockstep_gate();
    if (g == NG_LOCK_GATE_BUFFER) {
      continue;
    }
    if (g == NG_LOCK_GATE_STALL) {
      ok = false;
      break;
    }
    sim_tick++;
    mod_lockstep_set_step_tick(sim_tick);
    if (!mod_lockstep_have_input(mod_lockstep_local_peer_id(), sim_tick)) {
      ok = false;
      break;
    }
    mod_scene_runtime_use_server();
    mod_scene_physics_fixed_step(NG_MOD_FIXED_DT, true, true);
    const uint32_t hs = mod_scene_physics_checksum();
    mod_lockstep_on_stepped(sim_tick, hs);
  }
  if (ok && sim_tick < 120u) {
    ok = false;
  }
  if (ok) {
    mod_scene_runtime_use_server();
    NgSceneInst *box = mod_scene_graph_inst_by_key("box");
    if (!box || box->body_id_bits == 0 || fabsf(box->pos[1] - 1.0f) > 0.15f) {
      ok = false;
    }
  }
  // agent: composer-2.5 | 2026-07-30 | lockstep dual channel flush smoke | 26e91c
  /* Dual-channel: bodiless shared still flushes; lockstep bodies never do.
   * Smoke binary is NG_SERVER — take_flush only drains the server graph. */
  if (ok && !mod_scene_can_author(NG_SYNC_SHARED)) {
    ok = false;
  }
  if (ok) {
    mod_scene_runtime_use_server();
    NgSceneInst *box = mod_scene_graph_inst_by_key("box");
    if (!box) {
      ok = false;
    } else {
      char saved_body[32];
      const NgSyncMode saved_sync = box->sync;
      memcpy(saved_body, box->body, sizeof(saved_body));
      box->body[0] = '\0';
      box->sync = NG_SYNC_SHARED;
      mod_scene_graph_mark_dirty(box, NG_COMP_POS);
      NgStateUpdate u = {0};
      if (!mod_scene_take_flush(&u) || u.entity_id != box->id) {
        ok = false;
      }
      memcpy(box->body, saved_body, sizeof(box->body));
      box->sync = NG_SYNC_SHARED;
      mod_scene_graph_mark_dirty(box, NG_COMP_POS);
      if (ok && mod_scene_take_flush(&u)) {
        ok = false;
      }
      box->sync = saved_sync;
    }
  }

#if !defined(NG_SERVER)
  mod_scene_runtime_use_view();
  mod_scene_unload(mod_scene_runtime_scene());
#endif
  mod_scene_runtime_use_server();
  mod_scene_unload(mod_scene_runtime_scene());
  mod_lockstep_reset();
  ng_mod_set_fixed_gate(NULL);
  return ok;
}

// agent: composer-2.5 | 2026-08-01 | smoke both heap ball id | 78867a
static bool mod_scene_action_sim_id_smoke(void) {
  // agent: composer-2.5 | 2026-08-01 | smoke always dual heap ids | e0ca6e
  mod_lockstep_reset();
  ng_mod_set_fixed_gate(NULL);

  mod_scene_runtime_use_server();
  if (!mod_scene_begin("stacking", true, true)) {
    return false;
  }
  {
    ModSceneCtx *ctx = mod_scene_runtime_scene();
    NgSessionState session = {0};
    strncpy(session.scene_id, "stacking", sizeof(session.scene_id) - 1);
    session.lockstep = 2;
    mod_scene_push_session_obj(ctx, &session);
    if (!mod_scene_call_start(ctx)) {
      mod_scene_unload(ctx);
      return false;
    }
    mod_scene_drain_pending_change(ctx);
    ctx->started = true;
    mod_scene_physics_set_sim_mode(NG_PHYS_SIM_HYBRID);
  }

  mod_scene_runtime_use_view();
  if (!mod_scene_begin("stacking", false, true)) {
    mod_scene_runtime_use_server();
    mod_scene_unload(mod_scene_runtime_scene());
    return false;
  }
  {
    ModSceneCtx *ctx = mod_scene_runtime_scene();
    NgSessionState session = {0};
    strncpy(session.scene_id, "stacking", sizeof(session.scene_id) - 1);
    session.lockstep = 2;
    session.your_id = 1;
    session.controller_id = 1;
    mod_scene_push_session_obj(ctx, &session);
    if (!mod_scene_call_start(ctx)) {
      mod_scene_unload(ctx);
      mod_scene_runtime_use_server();
      mod_scene_unload(mod_scene_runtime_scene());
      return false;
    }
    mod_scene_drain_pending_change(ctx);
    ctx->started = true;
    mod_scene_physics_set_sim_mode(NG_PHYS_SIM_HYBRID);
  }

  mod_scene_lockstep_restart(1);
  mod_lockstep_set_local_peer(1);
  mod_lockstep_set_clock_owner(true);
  mod_lockstep_note_roster();

  const float argv[7] = {0.0f, 15.0f, 48.0f, 0.0f, 0.2f, -1.0f, 20.0f};
  mod_scene_runtime_use_server();
  if (!ng_jsact_call(g_scene_server.scene.ctx, "action_fire", 7, argv)) {
    mod_scene_runtime_use_view();
    mod_scene_unload(mod_scene_runtime_scene());
    mod_scene_runtime_use_server();
    mod_scene_unload(mod_scene_runtime_scene());
    return false;
  }
  NgSceneInst *sball = mod_scene_graph_inst_by_desc("shoot_ball_e");
  const uint32_t sid = sball ? sball->id : 0u;

  mod_scene_runtime_use_view();
  if (!ng_jsact_call(g_scene_view.scene.ctx, "action_fire", 7, argv)) {
    mod_scene_unload(mod_scene_runtime_scene());
    mod_scene_runtime_use_server();
    mod_scene_unload(mod_scene_runtime_scene());
    return false;
  }
  NgSceneInst *vball = mod_scene_graph_inst_by_desc("shoot_ball_e");
  const uint32_t vid = vball ? vball->id : 0u;

  bool ok = sid != 0u && sid == vid && mod_scene_graph_id_is_sim(sid);

  /* Soft PHYS clamps send tip to sim — propose must still land on a future tick. */
  // agent: composer-2.5 | 2026-08-01 | smoke two action fires | a60dcf
  if (ok) {
    const uint32_t sim = mod_lockstep_sim_tick() > 0 ? mod_lockstep_sim_tick() : 1u;
    mod_lockstep_on_soft_phys(sim);
    const float argv2[7] = {1.0f, 2.0f, 3.0f, 0.0f, 1.0f, 0.0f, 10.0f};
    const uint16_t aid = ng_jsact_hash_name("action_fire");
    if (!mod_lockstep_propose_local_action(aid, 7, argv2)) {
      ok = false;
    } else {
      NgLockAction got = {0};
      const uint32_t expect = sim + 1u;
      if (!mod_lockstep_action_for(mod_lockstep_local_peer_id(), expect, &got) || !got.present) {
        ok = false;
      }
    }
  }

  mod_scene_runtime_use_view();
  mod_scene_unload(mod_scene_runtime_scene());
  mod_scene_runtime_use_server();
  mod_scene_unload(mod_scene_runtime_scene());
  mod_lockstep_reset();
  ng_mod_set_fixed_gate(NULL);
  return ok;
}

bool mod_scene_smoke_test(void) {
  // agent: composer-2.5 | 2026-07-30 | lockstep dual channel flush smoke | 26e91c
  if (!mod_scene_smoke_one("cube", "cube_a_e", false)) {
    fprintf(stderr, "smoke fail: cube\n");
    return false;
  }
  if (!mod_scene_smoke_one("sphere", "sphere_a_e", true)) {
    fprintf(stderr, "smoke fail: sphere\n");
    return false;
  }
  if (!mod_scene_smoke_one("physics", "box_e", true)) {
    fprintf(stderr, "smoke fail: physics\n");
    return false;
  }
  // agent: composer-2.5 | 2026-08-01 | smoke stacking scene load | e196c4
  if (!mod_scene_smoke_one("stacking", "box_e", true)) {
    fprintf(stderr, "smoke fail: stacking\n");
    return false;
  }
  if (!mod_scene_smoke_one("owner", "owner_e", false)) {
    fprintf(stderr, "smoke fail: owner\n");
    return false;
  }
  if (!mod_scene_smoke_one("local", NULL, false)) {
    fprintf(stderr, "smoke fail: local\n");
    return false;
  }
  if (!mod_scene_lockstep_hash_smoke()) {
    fprintf(stderr, "smoke fail: lockstep_hash\n");
    return false;
  }
  // agent: composer-2.5 | 2026-08-01 | smoke both heap ball id | 78867a
  if (!mod_scene_action_sim_id_smoke()) {
    fprintf(stderr, "smoke fail: action_sim_id\n");
    return false;
  }
  return true;
}

// agent: composer-2.5 | 2026-07-29 | spawn opts key ordinal match | 701175
// agent: composer-2.5 | 2026-07-29 | view status text helper | d2e790
// agent: composer-2.5 | 2026-07-29 | host server view split | 1b39ad
// agent: composer-2.5 | 2026-07-28 | wire native scene fallback path | 096c5c
// agent: composer-2.5 | 2026-07-28 | test helpers only for fixtures | t4h5e6
// agent: composer-2.5 | 2026-07-28 | boot js outside scenes dir | b4o5o6
// agent: composer-2.5 | 2026-07-29 | view reset after scene begin | ef3462
// agent: composer-2.5 | 2026-07-29 | js change scene binding and drain | d70ea8
// agent: codex-5.3 | 2026-07-29 | add mouse ray plane helper | 27b035
// agent: codex-5.3 | 2026-07-29 | expose W S input keys | a3a058
// agent: codex-5.3 | 2026-07-29 | enable client-side raycast api | 9c4a01
// agent: codex-5.3 | 2026-07-29 | bind scene mouse raycast fn | b831e0
// agent: codex-5.3 | 2026-07-29 | export JS W S keycodes | 44cd8b
// agent: composer-2.5 | 2026-07-29 | author uses active runtime | b3f7a1
// agent: composer-2.5 | 2026-07-29 | flush shared from view graph | c8e4f0
// agent: composer-2.5 | 2026-07-29 | tick wired inputs once | 5a2d9c
// agent: composer-2.5 | 2026-07-29 | view entity transform observe | 4d8e21
// agent: composer-2.5 | 2026-07-29 | shared raycast plane helper | 7c1d4a
// agent: composer-2.5 | 2026-07-29 | expose JS mouse position | 8a4c2f
// agent: composer-2.5 | 2026-07-29 | position dirty deadband | 2c6e8a
// agent: composer-2.5 | 2026-07-29 | js get_position binding | 9b4d7e
// agent: composer-2.5 | 2026-07-29 | body shape fixed_step wire | 37245c
// agent: composer-2.5 | 2026-07-29 | server phys poses to view | c05110
// agent: composer-2.5 | 2026-07-29 | server phys snapshot to view | d2d78d
// agent: composer-2.5 | 2026-07-29 | lockstep scene sim flag | b3f626
// agent: composer-2.5 | 2026-07-29 | lockstep smoke hash test | 258f24
// agent: composer-2.5 | 2026-07-30 | lockstep ignore entity sync | ae273c
// agent: composer-2.5 | 2026-07-30 | lockstep activate no wipe | bb830d
// agent: composer-2.5 | 2026-07-30 | lockstep hash tick only | ca6913
// agent: composer-2.5 | 2026-07-30 | lockstep own sim clock | 5e56e9
// agent: composer-2.5 | 2026-07-30 | lockstep restart on load | 0de823
// agent: composer-2.5 | 2026-07-30 | narrow can_author lockstep bodies | 5153b8
// agent: composer-2.5 | 2026-07-30 | apply_remote skip lockstep bodies | 7b8dd8
// agent: composer-2.5 | 2026-07-30 | fixed_step body vs sync | 5afc64
// agent: composer-2.5 | 2026-07-30 | take_flush lockstep bodies only | bf6a08
// agent: composer-2.5 | 2026-07-30 | lockstep dual channel flush smoke | 26e91c
// agent: composer-2.5 | 2026-07-30 | render pose vel extrapolate | 25c348
// agent: composer-2.5 | 2026-07-30 | view apply push sample drive | 62a9c2

// agent: composer-2.5 | 2026-07-30 | host session syncing join | c76f26
// agent: composer-2.5 | 2026-07-30 | skip view phys step lockstep | 890ea4
// agent: composer-2.5 | 2026-07-30 | push lockstep poses to view | 25fe35
// agent: composer-2.5 | 2026-07-30 | get_input uses lockstep bits | 47960c
// agent: composer-2.5 | 2026-07-30 | get_input solo live fallback | 523e81
// agent: composer-2.5 | 2026-07-30 | get_input ORs all lockstep peers | b41de9
// agent: composer-2.5 | 2026-07-30 | shared apply ignores stale seq | 975e95
// agent: composer-2.5 | 2026-07-30 | apply_impulse JS binding | b980ed
// agent: composer-2.5 | 2026-07-30 | apply force torque JS bindings | cd7a18
// agent: composer-2.5 | 2026-07-30 | smoke single world input bits | 373a09
// agent: composer-2.5 | 2026-07-30 | smoke next sim tick bits | e7550b
// agent: composer-2.5 | 2026-07-30 | server entities text helper | 529fd4
// agent: composer-2.5 | 2026-07-30 | gravity vel mass js api | f956eb
// agent: composer-2.5 | 2026-07-30 | sphere describe parse | 741115
// agent: composer-2.5 | 2026-07-30 | set lockstep clock owner | d24ebb
// agent: composer-2.5 | 2026-07-30 | unload always clears lockstep gate | 314d8e
// agent: composer-2.5 | 2026-07-30 | get_input always lockstep slots | 861530
// agent: cursor-grok-4.5 | 2026-07-31 | view restart on scene change | abd7ca
// agent: cursor-grok-4.5 | 2026-07-31 | join_sync requires syncing flag | 73e30a
// agent: cursor-grok-4.5 | 2026-07-31 | allow same scene reload | 4ee66e
// agent: cursor-grok-4.5 | 2026-07-31 | scene epoch forces reload | 0ca291
// agent: composer-2.5 | 2026-07-31 | lockstep resim after confirm | bdb5d7
// agent: composer-2.5 | 2026-08-01 | hybrid sim js parse | 7b3cfd
// agent: composer-2.5 | 2026-08-01 | input sim family checks | 1297a4
// agent: composer-2.5 | 2026-08-01 | session mode apply fill | 4dc3e3
// agent: composer-2.5 | 2026-08-01 | owner reconcile skip small err | ea4522
// agent: composer-2.5 | 2026-08-01 | interest origin helper | 79d287
// agent: composer-2.5 | 2026-08-01 | smoke stacking scene load | e196c4
// agent: composer-2.5 | 2026-08-01 | js module wire host bindings | 884245
// agent: composer-2.5 | 2026-08-01 | action_register action bindings | 234498
// agent: composer-2.5 | 2026-08-01 | rename input get_local_any_peer | 827710
// agent: composer-2.5 | 2026-08-01 | view camera force view rt | f9c6af
// agent: composer-2.5 | 2026-08-01 | restore runtime after view cam | dd716e
// agent: composer-2.5 | 2026-08-01 | quiet tip-full propose warn | dd7b48
// agent: composer-2.5 | 2026-08-01 | keyed spawn reattach pose | 35782a
// agent: composer-2.5 | 2026-08-01 | push poses after both heaps | e810ca
// agent: composer-2.5 | 2026-08-01 | push prefer key no id | ad13dc
// agent: composer-2.5 | 2026-08-01 | spawn context sim id guards | 7c6221
// agent: composer-2.5 | 2026-08-01 | push by id after sim ids | 61401b
// agent: composer-2.5 | 2026-08-01 | smoke both heap ball id | 78867a
// agent: composer-2.5 | 2026-08-01 | smoke always dual heap ids | e0ca6e
// agent: composer-2.5 | 2026-08-01 | smoke two action fires | a60dcf
// agent: composer-2.5 | 2026-08-01 | lockstep bits for dedicated host | b1ef71
// agent: composer-2.5 | 2026-08-01 | push prefer key then id | 15a52d
