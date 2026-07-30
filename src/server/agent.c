// agent: composer-2.5 | 2026-07-25 | agent TCP JSON bridge | h1k39f
// agent: composer-2.5 | 2026-07-28 | agent port render snapshot | b598b6
#include "agent.h"
#include "engine/ng_action.h"
#include "engine/ng_bus.h"
#include "engine/ng_log.h"
#include "server/sim.h"
#include "scene/scene.h"
#include "scene/graph.h"
#include "scene/runtime.h"
#include "scene/physics.h"
#include "scene/lockstep.h"
#if !defined(NG_SERVER)
#include "client/input.h"
#include "client/render.h"
#include "net/mod_net.h"
#endif
#include "world/ng_world.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// agent: composer-2.5 | 2026-07-29 | mcp agent port probing | 8a1c2d
#define NG_AGENT_PROBE_MIN 27101
#define NG_AGENT_PROBE_MAX 27109

typedef struct ModAgentCtx {
  int listen_fd;
  int client_fd;
  char line_buf[4096];
  int line_len;
  char pending_reply[1024];
  bool have_reply;
  uint16_t port;
} ModAgentCtx;

static ModAgentCtx g_agent_ctx;
static uint16_t g_agent_port = NG_AGENT_DEFAULT_PORT;

void mod_agent_configure(uint16_t port) {
  if (port != 0) {
    g_agent_port = port;
  }
}

// agent: composer-2.5 | 2026-07-29 | expose listening port | 1b2c3d
uint16_t mod_agent_listening_port(void) { return g_agent_ctx.port; }

static void mod_agent_set_nonblock(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
}

static void mod_agent_set_nodelay(int fd) {
  const int yes = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
}

static void mod_agent_set_blocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
  }
}

static bool mod_agent_send_json(int fd, const char *json) {
  if (fd < 0 || !json) {
    return false;
  }
  const size_t n = strlen(json);
  char buf[4096];
  if (n + 2 >= sizeof(buf)) {
    return false;
  }
  memcpy(buf, json, n);
  buf[n] = '\n';
  // agent: composer-2.5 | 2026-07-25 | agent blocking send close | 671e9b
  mod_agent_set_blocking(fd);
  const ssize_t sent = send(fd, buf, n + 1, 0);
  mod_agent_set_nonblock(fd);
  return sent == (ssize_t)(n + 1);
}

// agent: composer-2.5 | 2026-07-25 | agent close fix no peek | cad4bd
static void mod_agent_close_client(ModAgentCtx *ctx) {
  if (ctx->client_fd < 0 || ctx->client_fd == ctx->listen_fd) {
    ctx->client_fd = -1;
    ctx->line_len = 0;
    return;
  }
  close(ctx->client_fd);
  ctx->client_fd = -1;
  ctx->line_len = 0;
}

static void mod_agent_handle_line(ModAgentCtx *ctx, const char *line) {
  char cmdline[512];
  cmdline[0] = '\0';
  static const char *argv_buf[NG_BUS_ARGV_MAX];

  const char *line_key = strstr(line, "\"line\"");
  if (line_key) {
    const char *q = strchr(line_key + 6, '"');
    if (q) {
      q++;
      const char *end = strchr(q, '"');
      if (end && (size_t)(end - q) < sizeof(cmdline)) {
        memcpy(cmdline, q, (size_t)(end - q));
        cmdline[end - q] = '\0';
      }
    }
  }

  if (cmdline[0] == '\0') {
    const char *cmd_key = strstr(line, "\"cmd\"");
    if (cmd_key) {
      const char *q = strchr(cmd_key + 5, '"');
      if (q) {
        q++;
        const char *end = strchr(q, '"');
        if (end && (size_t)(end - q) < sizeof(cmdline)) {
          memcpy(cmdline, q, (size_t)(end - q));
          cmdline[end - q] = '\0';
        }
      }
    }
  }

  if (strcmp(cmdline, "world_snapshot") == 0) {
    const NgWorld *w = mod_sim_world();
    char root_line[128];
    char view_line[128];
    char render_line[256];
    char vis_line[128];
    char gw_line[128];
#if !defined(NG_SERVER)
    mod_net_root_mirror_text(root_line, sizeof(root_line));
    mod_scene_view_status_text(view_line, sizeof(view_line));
    mod_render_snapshot_text(render_line, sizeof(render_line));
    mod_render_visibility_text(vis_line, sizeof(vis_line));
    mod_net_gateway_status_text(gw_line, sizeof(gw_line));
#else
    snprintf(root_line, sizeof(root_line), "root n/a");
    snprintf(view_line, sizeof(view_line), "view n/a");
    snprintf(render_line, sizeof(render_line), "render=n/a");
    snprintf(vis_line, sizeof(vis_line), "visible=n/a");
    snprintf(gw_line, sizeof(gw_line), "gateway n/a");
#endif
    char out[1800];
    snprintf(out, sizeof(out),
             "{\"ok\":true,\"text\":\"%s | local scene=%s entities=%d tick=%u | %s | %s | %s | "
             "%s\"}",
             root_line, mod_scene_current_id(), mod_scene_entity_count(), w ? w->tick : 0,
             view_line, render_line, vis_line, gw_line);
    mod_agent_send_json(ctx->client_fd, out);
    return;
  }

  if (strcmp(cmdline, "render_snapshot") == 0) {
    char render_line[256];
#if !defined(NG_SERVER)
    mod_render_snapshot_text(render_line, sizeof(render_line));
#else
    snprintf(render_line, sizeof(render_line), "render=n/a");
#endif
    char out[1200];
    snprintf(out, sizeof(out), "{\"ok\":true,\"text\":\"%s\"}", render_line);
    mod_agent_send_json(ctx->client_fd, out);
    return;
  }

  // agent: composer-2.5 | 2026-07-29 | mcp wire input transform observe | e7b3c1
  // agent: composer-2.5 | 2026-07-29 | mcp entity transforms server | 649b35
  if (strcmp(cmdline, "entity_transforms") == 0) {
    char entities[1500];
#if !defined(NG_SERVER)
    mod_scene_view_entities_text(entities, sizeof(entities));
#else
    {
      // agent: composer-2.5 | 2026-07-29 | mcp entity transforms server | 649b35
      size_t used = 0;
      mod_scene_runtime_use_server();
      const int n = mod_scene_graph_inst_count();
      used += (size_t)snprintf(entities + used, sizeof(entities) - used, "entities=%d", n);
      for (int i = 0; i < n && used + 1 < sizeof(entities); i++) {
        const NgSceneInst *inst = mod_scene_graph_inst_at(i);
        if (!inst) {
          continue;
        }
        used += (size_t)snprintf(
            entities + used, sizeof(entities) - used,
            " | id=%u desc=%s pos=%.3f,%.3f,%.3f rot=%.3f,%.3f,%.3f scale=%.3f", inst->id,
            inst->desc_name, inst->pos[0], inst->pos[1], inst->pos[2], inst->rot[0],
            inst->rot[1], inst->rot[2], inst->scale);
      }
    }
#endif
    char out[1800];
    snprintf(out, sizeof(out), "{\"ok\":true,\"text\":\"%s\"}", entities);
    mod_agent_send_json(ctx->client_fd, out);
    return;
  }

  // agent: composer-2.5 | 2026-07-29 | lockstep agent hash cmd | a8a2fc
  if (strcmp(cmdline, "lockstep_hash") == 0) {
    // agent: composer-2.5 | 2026-07-30 | lockstep gate diag | e4c1f4
    char out[320];
    mod_scene_runtime_use_server();
    const uint32_t hash = mod_scene_physics_checksum();
    const uint32_t tick = mod_lockstep_sim_tick();
    const uint32_t last_t = mod_lockstep_last_hash_tick();
    const uint32_t last_h = mod_lockstep_last_hash();
    uint32_t send = 0, peer = 0;
    int peers = 0, started = 0;
    mod_lockstep_debug(&send, &peers, &started, &peer);
    snprintf(out, sizeof(out),
             "{\"ok\":true,\"text\":\"lockstep active=%d tick=%u hash=0x%08x last_tick=%u "
             "last_hash=0x%08x send=%u peers=%d started=%d peer=%u\"}",
             mod_lockstep_active() ? 1 : 0, tick, hash, last_t, last_h, send, peers, started,
             peer);
    mod_agent_send_json(ctx->client_fd, out);
    return;
  }

  if (strncmp(cmdline, "wire_input", 10) == 0) {
#if defined(NG_SERVER)
    mod_agent_send_json(ctx->client_fd, "{\"ok\":false,\"error\":\"wire_input client only\"}");
    return;
#else
    int buttons = 0;
    int frames = 30;
    const char *p = cmdline + 10;
    while (*p) {
      while (*p == ' ' || *p == '\t') {
        p++;
      }
      if (*p == '\0') {
        break;
      }
      if (strncmp(p, "frames=", 7) == 0) {
        frames = atoi(p + 7);
        while (*p && *p != ' ' && *p != '\t') {
          p++;
        }
        continue;
      }
      if (*p == 'A' || *p == 'a') {
        buttons |= NG_INPUT_A;
      } else if (*p == 'D' || *p == 'd') {
        buttons |= NG_INPUT_D;
      } else if (*p == 'W' || *p == 'w') {
        buttons |= NG_INPUT_W;
      } else if (*p == 'S' || *p == 's') {
        buttons |= NG_INPUT_S;
      }
      while (*p && *p != ' ' && *p != '\t') {
        p++;
      }
    }
    mod_input_wire_buttons(buttons, frames);
    char out[256];
    snprintf(out, sizeof(out), "{\"ok\":true,\"text\":\"wired buttons=%d frames=%d\"}", buttons,
             frames);
    mod_agent_send_json(ctx->client_fd, out);
    return;
#endif
  }

  if (strncmp(cmdline, "wire_mouse", 10) == 0) {
#if defined(NG_SERVER)
    mod_agent_send_json(ctx->client_fd, "{\"ok\":false,\"error\":\"wire_mouse client only\"}");
    return;
#else
    float mx = 0.0f;
    float my = 0.0f;
    int frames = 30;
    int matched = sscanf(cmdline + 10, " %f %f frames=%d", &mx, &my, &frames);
    if (matched < 2) {
      matched = sscanf(cmdline + 10, " %f %f", &mx, &my);
    }
    if (matched < 2) {
      mod_agent_send_json(ctx->client_fd,
                          "{\"ok\":false,\"error\":\"usage: wire_mouse <x> <y> [frames=N]\"}");
      return;
    }
    if (matched < 3) {
      frames = 30;
    }
    mod_input_wire_mouse(mx, my, frames);
    char out[256];
    snprintf(out, sizeof(out), "{\"ok\":true,\"text\":\"wired mouse=%.1f,%.1f frames=%d\"}", mx, my,
             frames);
    mod_agent_send_json(ctx->client_fd, out);
    return;
#endif
  }

  // agent: composer-2.5 | 2026-07-30 | agent input phys debug cmds | f9a0d6
  if (strcmp(cmdline, "phys_debug") == 0) {
    char text[1600];
    mod_scene_phys_debug_text(text, sizeof(text));
    char out[1800];
    snprintf(out, sizeof(out), "{\"ok\":true,\"text\":\"%s\"}", text);
    mod_agent_send_json(ctx->client_fd, out);
    return;
  }

  if (strncmp(cmdline, "force_torque", 12) == 0) {
    char key[32] = "box";
    float tx = 0.0f, ty = 800.0f, tz = 0.0f;
    sscanf(cmdline + 12, " %31s %f %f %f", key, &tx, &ty, &tz);
    const bool ok = mod_scene_debug_apply_torque_key(key, tx, ty, tz);
    char out[256];
    snprintf(out, sizeof(out), "{\"ok\":%s,\"text\":\"torque %s %.1f,%.1f,%.1f\"}",
             ok ? "true" : "false", key, tx, ty, tz);
    mod_agent_send_json(ctx->client_fd, out);
    return;
  }

  // agent: composer-2.5 | 2026-07-29 | mcp raycast observe helper | 1a8c2e
  if (strncmp(cmdline, "raycast_plane_y", 15) == 0) {
    float plane_y = 0.0f;
    sscanf(cmdline + 15, " %f", &plane_y);
    char text[256];
    mod_scene_raycast_plane_y_text(plane_y, text, sizeof(text));
    char out[512];
    snprintf(out, sizeof(out), "{\"ok\":true,\"text\":\"%s\"}", text);
    mod_agent_send_json(ctx->client_fd, out);
    return;
  }

  if (strncmp(cmdline, "scene ", 6) == 0) {
#if defined(NG_HAS_EMBEDDED) && !defined(NG_SERVER)
    if (mod_net_upstream_connected()) {
      char reply[256];
      if (mod_net_gateway_upstream_cmd(cmdline, reply, sizeof(reply))) {
        char out[1200];
        snprintf(out, sizeof(out), "{\"ok\":true,\"text\":\"%s\"}",
                 reply[0] != '\0' ? reply : "upstream ok");
        mod_agent_send_json(ctx->client_fd, out);
        return;
      }
      mod_agent_send_json(ctx->client_fd, "{\"ok\":false,\"error\":\"upstream scene failed\"}");
      return;
    }
#endif
    argv_buf[0] = "scene";
    argv_buf[1] = cmdline + 6;
    NgMsg msg = {
        .kind = NG_MSG_CMD,
        .from = NG_BUS_AGENT,
        .to = NG_BUS_SIM,
        .argc = 2,
        .argv = argv_buf,
    };
    NgActionResult result = {0};
    if (!ng_action_server_exec(mod_sim_world(), &msg, 0, &result)) {
      mod_agent_send_json(ctx->client_fd, "{\"ok\":false,\"error\":\"scene exec failed\"}");
      return;
    }
    char out[1200];
    snprintf(out, sizeof(out), "{\"ok\":true,\"text\":\"%s\"}", result.reply);
    mod_agent_send_json(ctx->client_fd, out);
    return;
  }

  if (cmdline[0] == '\0') {
    mod_agent_send_json(ctx->client_fd, "{\"ok\":false,\"error\":\"missing cmd/line\"}");
    return;
  }

  ctx->have_reply = false;
  NgMsg msg = {
      .kind = NG_MSG_CMD,
      .from = NG_BUS_AGENT,
      .to = NG_BUS_SCRIPT,
      .line = cmdline,
  };
  ng_bus_publish(&msg);

  if (ctx->have_reply) {
    char out[1200];
    snprintf(out, sizeof(out), "{\"ok\":true,\"text\":\"%s\"}", ctx->pending_reply);
    mod_agent_send_json(ctx->client_fd, out);
  } else {
    mod_agent_send_json(ctx->client_fd, "{\"ok\":true}");
  }
}

static void mod_agent_poll_io(ModAgentCtx *ctx) {
  if (ctx->listen_fd < 0) {
    return;
  }

  if (ctx->client_fd < 0) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    const int fd = accept(ctx->listen_fd, (struct sockaddr *)&addr, &len);
    if (fd >= 0) {
      mod_agent_set_nonblock(fd);
      mod_agent_set_nodelay(fd);
      ctx->client_fd = fd;
      NG_LOG_INFO("agent client connected");
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
      return;
    }
  }

  if (ctx->client_fd < 0) {
    return;
  }

  char tmp[256];
  const ssize_t n = recv(ctx->client_fd, tmp, sizeof(tmp) - 1, 0);
  if (n <= 0) {
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return;
    }
    mod_agent_close_client(ctx);
    return;
  }
  tmp[n] = '\0';

  for (ssize_t i = 0; i < n; i++) {
    const char c = tmp[i];
    if (c == '\n') {
      ctx->line_buf[ctx->line_len] = '\0';
      mod_agent_handle_line(ctx, ctx->line_buf);
      ctx->line_len = 0;
      mod_agent_close_client(ctx);
      return;
    } else if (ctx->line_len < (int)sizeof(ctx->line_buf) - 1) {
      ctx->line_buf[ctx->line_len++] = c;
    }
  }
}

static bool mod_agent_on_msg(const NgMsg *msg, void *vctx) {
  ModAgentCtx *ctx = (ModAgentCtx *)vctx;
  if (!ctx || !msg) {
    return false;
  }

  if (msg->kind == NG_MSG_TICK) {
    if (msg->to != NG_BUS_ANY) {
      return false;
    }
    mod_agent_poll_io(ctx);
    return true;
  }
  if (msg->kind == NG_MSG_REPLY && msg->text) {
    strncpy(ctx->pending_reply, msg->text, sizeof(ctx->pending_reply) - 1);
    ctx->have_reply = true;
    return true;
  }
  return false;
}

static bool mod_agent_init(void *vctx) {
  ModAgentCtx *ctx = (ModAgentCtx *)vctx;
  memset(ctx, 0, sizeof(*ctx));
#if defined(NG_HAS_EMBEDDED) && !defined(NG_SERVER)
  if (g_agent_port == NG_AGENT_DEFAULT_PORT) {
    const uint16_t assigned = mod_net_assigned_agent_port();
    if (assigned != 0) {
      g_agent_port = assigned;
    } else if (mod_net_skip_local_boot()) {
      g_agent_port = NG_AGENT_LOCAL_PORT;
    }
  }
#endif
  ctx->port = g_agent_port;
  ctx->client_fd = -1;

  ctx->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (ctx->listen_fd < 0) {
    return false;
  }
  const int yes = 1;
  setsockopt(ctx->listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  mod_agent_set_nonblock(ctx->listen_fd);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  // agent: composer-2.5 | 2026-07-29 | probe free mcp port range | 1c9a3b
  // agent: composer-2.5 | 2026-07-29 | bind default mcp port first | d6e35f
  uint16_t start = ctx->port;
  uint16_t end = ctx->port;
  if (start == NG_AGENT_DEFAULT_PORT) {
    start = NG_AGENT_DEFAULT_PORT;
    end = NG_AGENT_PROBE_MAX;
  } else if (start >= NG_AGENT_PROBE_MIN && start <= NG_AGENT_PROBE_MAX) {
    end = NG_AGENT_PROBE_MAX;
  }

  int last_errno = 0;
  uint16_t chosen = 0;
  for (uint16_t p = start; p <= end; p++) {
    addr.sin_port = htons(p);
    if (bind(ctx->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
      chosen = p;
      break;
    }
    last_errno = errno;
    if (p == end) {
      close(ctx->listen_fd);
      ctx->listen_fd = -1;
      NG_LOG_WARN("agent bind failed %u..%u last=%u errno=%d", start, end, p,
                  last_errno);
      return false;
    }
    NG_LOG_WARN("agent bind busy try_next=%u errno=%d", (uint16_t)(p + 1),
                 last_errno);
  }

  if (chosen != 0) {
    ctx->port = chosen;
  }
  if (listen(ctx->listen_fd, 4) < 0) {
    close(ctx->listen_fd);
    ctx->listen_fd = -1;
    return false;
  }
  NG_LOG_INFO("agent tcp listening 127.0.0.1:%u", ctx->port);
  return true;
}

static void mod_agent_shutdown(void *vctx) {
  ModAgentCtx *ctx = (ModAgentCtx *)vctx;
  if (ctx->client_fd >= 0) {
    close(ctx->client_fd);
  }
  if (ctx->listen_fd >= 0) {
    close(ctx->listen_fd);
  }
  ctx->client_fd = -1;
  ctx->listen_fd = -1;
}

// agent: composer-2.5 | 2026-07-29 | Extend NgModOps side fixed_step | 7a4619
static const NgModOps g_agent_ops = {
    .name = "agent",
    .dest = NG_BUS_AGENT,
    .side = NG_MOD_SIDE_BOTH,
    .init = mod_agent_init,
    .shutdown = mod_agent_shutdown,
    .on_msg = mod_agent_on_msg,
    .fixed_step = NULL,
};

const NgModOps *mod_agent_ops(void) { return &g_agent_ops; }

void *mod_agent_ctx(void) { return &g_agent_ctx; }

// agent: composer-2.5 | 2026-07-26 | agent poll outside tick | d7e8f9
void mod_agent_poll(void) { mod_agent_poll_io(&g_agent_ctx); }

// agent: composer-2.5 | 2026-07-29 | mcp agent port probing | 8a1c2d
// agent: composer-2.5 | 2026-07-29 | expose listening port | 1b2c3d
// agent: composer-2.5 | 2026-07-29 | probe free mcp port range | 1c9a3b
// agent: composer-2.5 | 2026-07-29 | view gateway mcp fields | 72977c
// agent: composer-2.5 | 2026-07-28 | agent port render snapshot | b598b6
// agent: composer-2.5 | 2026-07-29 | mcp wire input transform observe | e7b3c1
// agent: composer-2.5 | 2026-07-29 | mcp raycast observe helper | 1a8c2e
// agent: composer-2.5 | 2026-07-29 | Extend NgModOps side fixed_step | 7a4619
// agent: composer-2.5 | 2026-07-29 | bind default mcp port first | d6e35f
// agent: composer-2.5 | 2026-07-29 | mcp entity transforms server | 649b35
// agent: composer-2.5 | 2026-07-29 | lockstep agent hash cmd | a8a2fc
// agent: composer-2.5 | 2026-07-30 | lockstep gate diag | e4c1f4
