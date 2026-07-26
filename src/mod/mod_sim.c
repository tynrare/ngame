// agent: composer-2.5 | 2026-07-25 | server sim bus module | a9d62e
#include "mod_sim.h"
#ifdef NG_SERVER
#include "core/ng_action.h"
#include "mod/mod_net.h"
#include "mod/mod_render.h"
#elif defined(NG_HAS_EMBEDDED)
#include "core/ng_action.h"
#include "core/ng_embed.h"
#include "mod/mod_net.h"
#include "mod/mod_render.h"
#endif
#include "core/ng_bus.h"
#include "core/ng_log.h"
#include "core/ng_session.h"
#include "sim/sim_types.h"
#include "world/ng_world.h"
#include <stdio.h>
#include <string.h>

#define MOD_SIM_REGISTRY_MAX 8

typedef struct ModSimSlot {
  const SimOps *ops;
} ModSimSlot;

typedef struct ModSimCtx {
  NgWorld world;
  ModSimSlot registry[MOD_SIM_REGISTRY_MAX];
  int registry_count;
  const SimOps *active;
  char feedback[256];
  NgSnapshot snapshot;
  NgSnapshot baseline;
  NgSnapshot wire_snap;
  float snap_accum;
} ModSimCtx;

#define NG_SNAPSHOT_HZ 20.0f

static ModSimCtx g_sim_ctx;

NgWorld *mod_sim_world(void) { return &g_sim_ctx.world; }

static bool mod_sim_register(ModSimCtx *ctx, const SimOps *ops) {
  if (!ctx || !ops || !ops->id || ctx->registry_count >= MOD_SIM_REGISTRY_MAX) {
    return false;
  }
  ctx->registry[ctx->registry_count++].ops = ops;
  return true;
}

static const SimOps *mod_sim_find(ModSimCtx *ctx, const char *id) {
  for (int i = 0; i < ctx->registry_count; i++) {
    if (strcmp(ctx->registry[i].ops->id, id) == 0) {
      return ctx->registry[i].ops;
    }
  }
  return NULL;
}

static bool mod_sim_load(ModSimCtx *ctx, const char *id) {
  const SimOps *ops = mod_sim_find(ctx, id);
  if (!ops) {
    snprintf(ctx->feedback, sizeof(ctx->feedback), "unknown scene: %s", id);
    return false;
  }
  if (ctx->active && ctx->active->exit) {
    ctx->active->exit(&ctx->world);
  }
  ng_world_set_scene(&ctx->world, id);
  ctx->active = ops;
  if (!ops->enter(&ctx->world)) {
    snprintf(ctx->feedback, sizeof(ctx->feedback), "scene enter failed: %s", id);
    ctx->active = NULL;
    return false;
  }
  snprintf(ctx->feedback, sizeof(ctx->feedback), "scene loaded: %s", id);
  NG_LOG_INFO("%s", ctx->feedback);
  return true;
}

// agent: composer-2.5 | 2026-07-25 | sim snapshot scratch reuse | 3c08c0
static void mod_sim_publish_snapshot(ModSimCtx *ctx) {
#if defined(NG_HAS_EMBEDDED)
  if (mod_net_is_embedded()) {
    return;
  }
#endif
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
  if (!mod_net_has_clients()) {
    return;
  }
#endif
  // agent: composer-2.5 | 2026-07-26 | full snapshot not aoi hack | e5f6a7
  ng_world_fill_snapshot(&ctx->world, &ctx->snapshot);
  if (ctx->baseline.tick > 0 && strcmp(ctx->snapshot.scene_id, ctx->baseline.scene_id) == 0) {
    ng_world_fill_snapshot_delta(&ctx->world, &ctx->snapshot, &ctx->baseline, &ctx->wire_snap);
    if (ctx->wire_snap.entity_count == 0) {
      return;
    }
  }
  ctx->baseline = ctx->snapshot;

  NgMsg snap = {
      .kind = NG_MSG_SNAPSHOT,
      .from = NG_BUS_SIM,
      .to = NG_BUS_NET,
      .snapshot = &ctx->snapshot,
  };
  ng_bus_publish(&snap);
}

bool mod_sim_run_cmd(const NgMsg *msg, char *reply, size_t reply_cap) {
  ModSimCtx *ctx = &g_sim_ctx;
  if (!msg || !reply || reply_cap == 0) {
    return false;
  }
  if (msg->line && strcmp(msg->line, "__agent_snapshot__") == 0) {
    snprintf(reply, reply_cap, "scene=%s entities=%d tick=%u", ctx->world.scene_id,
             ctx->world.live_count, ctx->world.tick);
    return true;
  }
  if (msg->argc <= 0 || !msg->argv[0]) {
    return false;
  }
  if (strcmp(msg->argv[0], "scene") != 0) {
    return false;
  }
  if (msg->argc < 2) {
    snprintf(reply, reply_cap, "usage: scene <id>");
    return true;
  }
  mod_sim_load(ctx, msg->argv[1]);
  strncpy(reply, ctx->feedback, reply_cap - 1);
  reply[reply_cap - 1] = '\0';
  return true;
}

static bool mod_sim_handle_cmd(ModSimCtx *ctx, const NgMsg *msg) {
  if (!msg) {
    return false;
  }
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
  NgActionResult result = {0};
  if (!ng_action_server_exec(&ctx->world, msg, 0, &result)) {
    return false;
  }
#if defined(NG_HAS_EMBEDDED)
  if (mod_net_is_embedded()) {
    mod_render_apply_action(&result);
    NgMsg reply = {
        .kind = NG_MSG_REPLY,
        .from = NG_BUS_SIM,
        .to = NG_BUS_CONSOLE,
        .text = result.reply,
    };
    ng_bus_publish(&reply);
    return true;
  }
#endif
#if defined(NG_SERVER)
  NgMsg out = {
      .kind = NG_MSG_ACTION_RESULT,
      .from = NG_BUS_SIM,
      .to = NG_BUS_NET,
      .action_result = &result,
  };
  ng_bus_publish(&out);
#endif
  return true;
#else
  (void)ctx;
  return false;
#endif
}

static bool mod_sim_on_msg(const NgMsg *msg, void *vctx) {
  ModSimCtx *ctx = (ModSimCtx *)vctx;
  if (!ctx || !msg) {
    return false;
  }

  switch (msg->kind) {
  case NG_MSG_CMD:
    return mod_sim_handle_cmd(ctx, msg);
  case NG_MSG_INPUT:
    if (!ng_scene_client_fields(ctx->world.scene_id)) {
      ng_world_apply_input(&ctx->world, msg->input_buttons, msg->input_yaw_delta);
    }
    return true;
  case NG_MSG_TICK:
    if (msg->to != NG_BUS_SIM) {
      return false;
    }
#if defined(NG_HAS_EMBEDDED)
    if (mod_net_is_embedded()) {
      return false;
    }
#endif
    if (!ng_bus_gate(NG_BUS_SIM)) {
      return false;
    }
    ctx->world.tick++;
    if (ctx->active && ctx->active->update) {
      ctx->active->update(&ctx->world, msg->dt);
    }
    ctx->snap_accum += msg->dt;
    if (ctx->snap_accum >= (1.0f / NG_SNAPSHOT_HZ)) {
      ctx->snap_accum = 0.0f;
      mod_sim_publish_snapshot(ctx);
    }
    return true;
  case NG_MSG_SHUTDOWN:
    if (ctx->active && ctx->active->exit) {
      ctx->active->exit(&ctx->world);
    }
    return true;
  default:
    return false;
  }
}

static bool mod_sim_init(void *vctx) {
  ModSimCtx *ctx = (ModSimCtx *)vctx;
  memset(ctx, 0, sizeof(*ctx));
  ng_world_init(&ctx->world);
  mod_sim_register(ctx, sim_sphere_ops());
  mod_sim_register(ctx, sim_cube_ops());
  if (!mod_sim_load(ctx, "sphere")) {
    return false;
  }
#if defined(NG_HAS_EMBEDDED)
  if (mod_net_is_embedded()) {
    ng_embed_bind(&ctx->world);
  }
#endif
  return true;
}

static void mod_sim_shutdown(void *vctx) {
  ModSimCtx *ctx = (ModSimCtx *)vctx;
  if (ctx->active && ctx->active->exit) {
    ctx->active->exit(&ctx->world);
  }
  ctx->active = NULL;
}

static const NgModOps g_sim_ops = {
    .name = "sim",
    .dest = NG_BUS_SIM,
    .init = mod_sim_init,
    .shutdown = mod_sim_shutdown,
    .on_msg = mod_sim_on_msg,
};

const NgModOps *mod_sim_ops(void) { return &g_sim_ops; }

void *mod_sim_ctx(void) { return &g_sim_ctx; }
