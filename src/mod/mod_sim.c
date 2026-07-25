// agent: composer-2.5 | 2026-07-25 | server sim bus module | a9d62e
#include "mod_sim.h"
#ifdef NG_SERVER
#include "mod/mod_net.h"
#endif
#include "core/ng_bus.h"
#include "core/ng_log.h"
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

static void mod_sim_reply(ModSimCtx *ctx) {
  NgMsg reply = {
      .kind = NG_MSG_REPLY,
      .from = NG_BUS_SIM,
#ifdef NG_SERVER
      .to = NG_BUS_ANY,
#else
      .to = NG_BUS_CONSOLE,
#endif
      .text = ctx->feedback,
  };
  ng_bus_publish(&reply);
}

// agent: composer-2.5 | 2026-07-25 | skip unchanged sim snap | 18fe4e
static void mod_sim_publish_snapshot(ModSimCtx *ctx) {
#ifdef NG_SERVER
  if (!mod_net_has_clients()) {
    return;
  }
#endif
  NgSnapshot full = {0};
  ng_world_fill_snapshot_aoi(&ctx->world, &full, 0.0f, 0.0f, 99999.0f, ctx->world.tick);
  if (ctx->baseline.tick > 0 && strcmp(full.scene_id, ctx->baseline.scene_id) == 0) {
    NgSnapshot wire = {0};
    ng_world_fill_snapshot_delta(&ctx->world, &full, &ctx->baseline, &wire);
    if (wire.entity_count == 0) {
      return;
    }
  }
  ctx->snapshot = full;
  ctx->baseline = full;

  NgMsg snap = {
      .kind = NG_MSG_SNAPSHOT,
      .from = NG_BUS_SIM,
      .to = NG_BUS_NET,
      .snapshot = &ctx->snapshot,
  };
  ng_bus_publish(&snap);
}

static bool mod_sim_handle_cmd(ModSimCtx *ctx, const NgMsg *msg) {
  if (!msg) {
    return false;
  }
  if (msg->line && strcmp(msg->line, "__agent_snapshot__") == 0) {
    snprintf(ctx->feedback, sizeof(ctx->feedback), "scene=%s entities=%d tick=%u",
             ctx->world.scene_id, ctx->world.live_count, ctx->world.tick);
    mod_sim_reply(ctx);
    return true;
  }
  if (msg->argc <= 0 || !msg->argv[0]) {
    return false;
  }
  if (strcmp(msg->argv[0], "scene") != 0) {
    return false;
  }
  if (msg->argc < 2) {
    snprintf(ctx->feedback, sizeof(ctx->feedback), "usage: scene <id>");
    mod_sim_reply(ctx);
    return true;
  }
  mod_sim_load(ctx, msg->argv[1]);
  mod_sim_reply(ctx);
  mod_sim_publish_snapshot(ctx);

  NgMsg ev = {
      .kind = NG_MSG_EVENT,
      .from = NG_BUS_SIM,
      .to = NG_BUS_NET,
      .text = ctx->world.scene_id,
  };
  ng_bus_publish(&ev);
  return true;
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
    ng_world_apply_input(&ctx->world, msg->input_buttons, msg->input_yaw_delta);
    return true;
  case NG_MSG_TICK:
    if (msg->to != NG_BUS_SIM) {
      return false;
    }
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
  return mod_sim_load(ctx, "sphere");
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
