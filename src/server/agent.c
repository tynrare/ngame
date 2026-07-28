// agent: composer-2.5 | 2026-07-25 | agent TCP JSON bridge | h1k39f
// agent: composer-2.5 | 2026-07-25 | agent scene sync action exec | e8f9a0
#include "agent.h"
#include "engine/ng_action.h"
#include "engine/ng_bus.h"
#include "engine/ng_log.h"
#include "server/sim.h"
#include "scene/scene.h"
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
    char out[1200];
    snprintf(out, sizeof(out),
             "{\"ok\":true,\"text\":\"scene=%s entities=%d tick=%u\"}",
             mod_scene_current_id(), mod_scene_entity_count(), w ? w->tick : 0);
    mod_agent_send_json(ctx->client_fd, out);
    return;
  }

  if (strncmp(cmdline, "scene ", 6) == 0) {
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
  ctx->port = NG_AGENT_DEFAULT_PORT;
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
  addr.sin_port = htons(ctx->port);
  if (bind(ctx->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(ctx->listen_fd);
    ctx->listen_fd = -1;
    return false;
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

static const NgModOps g_agent_ops = {
    .name = "agent",
    .dest = NG_BUS_AGENT,
    .init = mod_agent_init,
    .shutdown = mod_agent_shutdown,
    .on_msg = mod_agent_on_msg,
};

const NgModOps *mod_agent_ops(void) { return &g_agent_ops; }

void *mod_agent_ctx(void) { return &g_agent_ctx; }

// agent: composer-2.5 | 2026-07-26 | agent poll outside tick | d7e8f9
void mod_agent_poll(void) { mod_agent_poll_io(&g_agent_ctx); }
