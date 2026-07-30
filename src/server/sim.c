// agent: composer-2.5 | 2026-07-27 | js scene sim host only | a4b5c6
// agent: composer-2.5 | 2026-07-28 | sim gateway authority gate | 7cd0be
// agent: composer-2.5 | 2026-07-29 | scene load helper for js route | 687e07
#include "sim.h"
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
#include "engine/ng_action.h"
#include "net/mod_net.h"
#endif
#include "engine/ng_bus.h"
#include "engine/ng_log.h"
#include "scene/scene.h"
#include "world/ng_world.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct ModSimCtx {
  NgWorld world;
  char feedback[256];
} ModSimCtx;

static ModSimCtx g_sim_ctx;

NgWorld *mod_sim_world(void) { return &g_sim_ctx.world; }

static bool mod_sim_load(ModSimCtx *ctx, const char *id) {
  if (!mod_scene_load(id)) {
    snprintf(ctx->feedback, sizeof(ctx->feedback), "scene load failed: %s", id);
    return false;
  }
  ng_world_set_scene(&ctx->world, id);
  mod_scene_mirror_server(&ctx->world);
  snprintf(ctx->feedback, sizeof(ctx->feedback), "scene loaded: %s", id);
  NG_LOG_INFO("%s", ctx->feedback);
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
  mod_net_broadcast_scene_session();
#endif
  return true;
}

// agent: composer-2.5 | 2026-07-29 | scene load helper for js route | 687e07
bool mod_sim_load_scene(const char *id, char *reply, size_t reply_cap) {
  ModSimCtx *ctx = &g_sim_ctx;
  if (!id || id[0] == '\0') {
    if (reply && reply_cap > 0) {
      snprintf(reply, reply_cap, "scene load failed: missing id");
    }
    return false;
  }
  if (!mod_sim_load(ctx, id)) {
    if (reply && reply_cap > 0) {
      strncpy(reply, ctx->feedback, reply_cap - 1);
      reply[reply_cap - 1] = '\0';
    }
    return false;
  }
  if (reply && reply_cap > 0) {
    strncpy(reply, ctx->feedback, reply_cap - 1);
    reply[reply_cap - 1] = '\0';
  }
  return true;
}

bool mod_sim_run_cmd(const NgMsg *msg, char *reply, size_t reply_cap) {
  ModSimCtx *ctx = &g_sim_ctx;
  if (!msg || !reply || reply_cap == 0) {
    return false;
  }
  if (msg->line && strcmp(msg->line, "__agent_snapshot__") == 0) {
    snprintf(reply, reply_cap, "scene=%s entities=%d tick=%u", mod_scene_current_id(),
             mod_scene_entity_count(), ctx->world.tick);
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
  mod_sim_load_scene(msg->argv[1], reply, reply_cap);
  return true;
}

static bool mod_sim_handle_cmd(ModSimCtx *ctx, const NgMsg *msg) {
  if (!msg) {
    return false;
  }
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
#if defined(NG_HAS_EMBEDDED) && !defined(NG_SERVER)
  if (mod_net_upstream_connected()) {
    char reply[256];
    if (msg->line && mod_net_gateway_upstream_cmd(msg->line, reply, sizeof(reply))) {
      NgMsg out = {
          .kind = NG_MSG_REPLY,
          .from = NG_BUS_SIM,
          .to = NG_BUS_AGENT,
          .text = reply,
      };
      ng_bus_publish(&out);
      return true;
    }
    return false;
  }
#endif
  NgActionResult result = {0};
  if (!ng_action_server_exec(&ctx->world, msg, 0, &result)) {
    return false;
  }
  NgMsg out = {
      .kind = NG_MSG_ACTION_RESULT,
      .from = NG_BUS_SIM,
      .to = NG_BUS_NET,
      .action_result = &result,
  };
  ng_bus_publish(&out);
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
  case NG_MSG_TICK:
    if (msg->to != NG_BUS_SIM) {
      return false;
    }
#if defined(NG_HAS_EMBEDDED) && !defined(NG_SERVER)
    if (mod_net_upstream_connected()) {
      return false;
    }
#endif
    if (!ng_bus_gate(NG_BUS_SIM)) {
      return false;
    }
    ctx->world.tick++;
    return true;
  case NG_MSG_SHUTDOWN:
    mod_scene_load("");
    return true;
  default:
    return false;
  }
}

static bool mod_sim_init(void *vctx) {
  ModSimCtx *ctx = (ModSimCtx *)vctx;
  memset(ctx, 0, sizeof(*ctx));
  ng_world_init(&ctx->world);
#if defined(NG_HAS_EMBEDDED) && !defined(NG_SERVER)
  if (mod_net_skip_local_boot()) {
    return true;
  }
#endif
  if (!mod_scene_load_boot()) {
    return false;
  }
  ng_world_set_scene(&ctx->world, "boot");
  return true;
}

static void mod_sim_shutdown(void *vctx) {
  ModSimCtx *ctx = (ModSimCtx *)vctx;
  (void)ctx;
  mod_scene_load("");
}

// agent: composer-2.5 | 2026-07-29 | Extend NgModOps side fixed_step | 257ba6
static const NgModOps g_sim_ops = {
    .name = "sim",
    .dest = NG_BUS_SIM,
    .side = NG_MOD_SIDE_SERVER,
    .init = mod_sim_init,
    .shutdown = mod_sim_shutdown,
    .on_msg = mod_sim_on_msg,
    .fixed_step = NULL,
};

const NgModOps *mod_sim_ops(void) { return &g_sim_ops; }

void *mod_sim_ctx(void) { return &g_sim_ctx; }

// agent: composer-2.5 | 2026-07-28 | sim gateway authority gate | 7cd0be
// agent: composer-2.5 | 2026-07-29 | Extend NgModOps side fixed_step | 257ba6
// agent: composer-2.5 | 2026-07-29 | scene load helper for js route | 687e07
