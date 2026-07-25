// agent: composer-2.5 | 2026-07-25 | net bus bridge module | f9i17d
#include "mod_net.h"
#include "core/ng_bus.h"
#include "core/ng_log.h"
#include "core/ng_proto.h"
#include "net/ng_net.h"
#ifdef NG_SERVER
#include "mod/mod_sim.h"
#include "net/ng_ws_server.h"
#endif
#include "world/ng_world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NG_SERVER
#include <raylib.h>
#include <unistd.h>
#endif

#define NG_INPUT_A 1
#define NG_INPUT_D 2

#ifdef NG_SERVER
typedef struct NetPeerState {
  NgSnapshot baseline;
  bool have_baseline;
  uint16_t seq;
} NetPeerState;
#endif

typedef struct ModNetCtx {
  NgNet *net;
#ifdef NG_SERVER
  NgWsServer *ws;
  bool ws_was_connected;
#endif
  char host[64];
  uint16_t port;
  uint16_t seq;
  NgSnapshot snapshot_buf;
  NgSnapshot baseline;
  bool have_baseline;
  uint32_t last_snap_tick;
} ModNetCtx;

#ifdef NG_SERVER
static void mod_net_send_snapshot_peer(NgNet *net, NgNetPeer *peer, void *vctx);
static void mod_net_send_ws_snapshot(ModNetCtx *ctx, const NgSnapshot *full);
#endif

static ModNetCtx g_net_ctx;
#ifndef NG_SERVER
static double g_connect_t0 = 0.0;
static bool g_connect_t0_set = false;
#endif

void mod_net_configure(const char *host, uint16_t port) {
  if (host) {
    strncpy(g_net_ctx.host, host, sizeof(g_net_ctx.host) - 1);
  } else {
    strncpy(g_net_ctx.host, NG_NET_HOST, sizeof(g_net_ctx.host) - 1);
  }
  g_net_ctx.port = port ? port : NG_NET_DEFAULT_PORT;
}

bool mod_net_is_connected(void) {
  return g_net_ctx.net != NULL && ng_net_connected(g_net_ctx.net);
}

#ifdef NG_SERVER
// agent: composer-2.5 | 2026-07-25 | skip idle snapshot spam | 1a47a5
bool mod_net_has_clients(void) {
  if (g_net_ctx.net && ng_net_connected(g_net_ctx.net)) {
    return true;
  }
  if (g_net_ctx.ws && ng_ws_server_connected(g_net_ctx.ws)) {
    return true;
  }
  return false;
}
#endif

// agent: composer-2.5 | 2026-07-25 | mod_net_endpoint helper | 11b35c
void mod_net_endpoint(char *host, size_t host_cap, uint16_t *port) {
  if (host && host_cap > 0) {
    strncpy(host, g_net_ctx.host, host_cap - 1);
    host[host_cap - 1] = '\0';
  }
  if (port) {
    *port = g_net_ctx.port;
  }
}

#ifndef NG_SERVER
// agent: composer-2.5 | 2026-07-25 | mod_net_connect_elapsed | a5317b
double mod_net_connect_elapsed(void) {
  if (!g_connect_t0_set) {
    return 0.0;
  }
  return GetTime() - g_connect_t0;
}
#endif

static void mod_net_handle_packet(NgNet *net, NgNetPeer *peer, const uint8_t *data,
                                  size_t len, uint8_t channel, void *vctx) {
  ModNetCtx *ctx = (ModNetCtx *)vctx;
  (void)net;
  (void)peer;
  (void)channel;
  if (!data || len < sizeof(NgProtoHeader)) {
    return;
  }

  NgProtoBuf buf = {.len = len, .pos = sizeof(NgProtoHeader)};
  memcpy(buf.data, data, len);

  NgProtoHeader h;
  buf.pos = 0;
  if (!ng_proto_read_header(&buf, &h) || h.magic != NG_PROTO_MAGIC) {
    return;
  }

  switch (h.type) {
#ifdef NG_SERVER
  case NG_PKT_INPUT: {
    uint16_t seq = 0;
    uint32_t tick = 0;
    int buttons = 0;
    float yaw = 0.0f;
    if (!ng_proto_decode_input(&buf, &seq, &tick, &buttons, &yaw)) {
      return;
    }
    NgMsg msg = {
        .kind = NG_MSG_INPUT,
        .from = NG_BUS_NET,
        .to = NG_BUS_SIM,
        .input_buttons = buttons,
        .input_yaw_delta = yaw,
        .input_seq = seq,
    };
    ng_bus_publish(&msg);
    break;
  }
  case NG_PKT_CMD: {
    char line[256];
    if (!ng_proto_decode_cmd(&buf, line, sizeof(line))) {
      return;
    }
    // agent: composer-2.5 | 2026-07-25 | route scene cmd to sim | ea4769
    NgBusDest dest = NG_BUS_SCRIPT;
    if (strncmp(line, "scene ", 6) == 0) {
      dest = NG_BUS_SIM;
    }
    NgMsg msg = {
        .kind = NG_MSG_CMD,
        .from = NG_BUS_NET,
        .to = dest,
        .line = line,
    };
    if (dest == NG_BUS_SIM) {
      static const char *argv_buf[NG_BUS_ARGV_MAX];
      argv_buf[0] = "scene";
      argv_buf[1] = line + 6;
      msg.argc = 2;
      msg.argv = argv_buf;
      msg.line = NULL;
    }
    ng_bus_publish(&msg);
    break;
  }
#else
  case NG_PKT_SNAPSHOT: {
    bool delta = false;
    NgSnapshot snap = ctx->have_baseline ? ctx->baseline : (NgSnapshot){0};
    if (!ng_proto_decode_snapshot(&buf, &snap, &delta)) {
      return;
    }
    ctx->snapshot_buf = snap;
    ctx->baseline = snap;
    ctx->have_baseline = true;
    ctx->last_snap_tick = snap.tick;

    NgMsg msg = {
        .kind = NG_MSG_SNAPSHOT,
        .from = NG_BUS_NET,
        .to = NG_BUS_RENDER,
        .snapshot = &ctx->snapshot_buf,
    };
    ng_bus_publish(&msg);
    break;
  }
  case NG_PKT_CMD_REPLY: {
    char text[1024];
    if (!ng_proto_decode_text(&buf, text, sizeof(text))) {
      return;
    }
    NgMsg msg = {
        .kind = NG_MSG_REPLY,
        .from = NG_BUS_NET,
        .to = NG_BUS_CONSOLE,
        .text = text,
    };
    ng_bus_publish(&msg);
    break;
  }
  case NG_PKT_EVENT: {
    char text[256];
    if (!ng_proto_decode_text(&buf, text, sizeof(text))) {
      return;
    }
    NgMsg msg = {
        .kind = NG_MSG_EVENT,
        .from = NG_BUS_NET,
        .to = NG_BUS_RENDER,
        .text = text,
    };
    ng_bus_publish(&msg);
    break;
  }
#endif
  default:
    break;
  }
}

// agent: composer-2.5 | 2026-07-25 | per-client snapshot baselines | acee14
#ifdef NG_SERVER
// agent: composer-2.5 | 2026-07-25 | snapshot on connect send | d4c470
static void mod_net_on_peer(NgNet *net, NgNetPeer *peer, bool connected, void *vctx) {
  ModNetCtx *ctx = (ModNetCtx *)vctx;
  if (connected) {
    NetPeerState *ps = (NetPeerState *)calloc(1, sizeof(NetPeerState));
    ng_net_peer_set_data(peer, ps);
    NgSnapshot full = {0};
    ng_world_fill_snapshot(mod_sim_world(), &full);
    ctx->snapshot_buf = full;
    mod_net_send_snapshot_peer(net, peer, ctx);
    ng_net_flush(net);
  } else {
    free(ng_net_peer_data(peer));
    ng_net_peer_set_data(peer, NULL);
  }
}

// agent: composer-2.5 | 2026-07-25 | snapshots udp skip unchanged | c30cbd
static void mod_net_send_snapshot_peer(NgNet *net, NgNetPeer *peer, void *vctx) {
  ModNetCtx *ctx = (ModNetCtx *)vctx;
  const NgSnapshot *full = &ctx->snapshot_buf;
  NetPeerState *ps = (NetPeerState *)ng_net_peer_data(peer);
  if (!ps) {
    return;
  }

  NgSnapshot wire = {0};
  const NgSnapshot *send = full;
  bool delta = false;
  if (ps->have_baseline && strcmp(ps->baseline.scene_id, full->scene_id) != 0) {
    send = full;
    delta = false;
  } else if (ps->have_baseline && ps->baseline.tick > 0) {
    ng_world_fill_snapshot_delta(mod_sim_world(), (NgSnapshot *)full, &ps->baseline, &wire);
    if (wire.entity_count == 0) {
      return;
    }
    send = &wire;
    delta = true;
  }

  NgProtoBuf buf;
  if (!ng_proto_encode_snapshot(&buf, send, ++ps->seq, delta)) {
    return;
  }
  ng_net_send_to(net, peer, buf.data, buf.len, NG_CH_UNRELIABLE, false);
  ps->baseline = *full;
  ps->have_baseline = true;
}

static void mod_net_send_ws_snapshot(ModNetCtx *ctx, const NgSnapshot *full) {
  if (!ctx->ws || !full) {
    return;
  }
  NgSnapshot wire = {0};
  const NgSnapshot *send = full;
  bool delta = false;
  if (ctx->have_baseline && strcmp(ctx->baseline.scene_id, full->scene_id) != 0) {
    send = full;
    delta = false;
  } else if (ctx->have_baseline && ctx->baseline.tick > 0) {
    ng_world_fill_snapshot_delta(mod_sim_world(), (NgSnapshot *)full, &ctx->baseline, &wire);
    if (wire.entity_count == 0) {
      return;
    }
    send = &wire;
    delta = true;
  }
  NgProtoBuf buf;
  if (!ng_proto_encode_snapshot(&buf, send, ++ctx->seq, delta)) {
    return;
  }
  ng_ws_server_send(ctx->ws, buf.data, buf.len);
  ctx->baseline = *full;
  ctx->have_baseline = true;
}

static void mod_net_handle_ws_packet(const uint8_t *data, size_t len, void *vctx) {
  mod_net_handle_packet(NULL, NULL, data, len, NG_CH_RELIABLE, vctx);
}
#endif

#ifndef NG_SERVER
static void mod_net_send_input(ModNetCtx *ctx) {
  if (!ng_net_connected(ctx->net)) {
    return;
  }
  int buttons = 0;
  float yaw = 0.0f;
  if (IsKeyDown(KEY_A)) {
    buttons |= NG_INPUT_A;
  }
  if (IsKeyDown(KEY_D)) {
    buttons |= NG_INPUT_D;
  }
  if (IsKeyDown(KEY_LEFT)) {
    yaw -= 0.05f;
  }
  if (IsKeyDown(KEY_RIGHT)) {
    yaw += 0.05f;
  }

  NgProtoBuf buf;
  if (!ng_proto_encode_input(&buf, ++ctx->seq, ctx->last_snap_tick, buttons, yaw)) {
    return;
  }
  ng_net_send(ctx->net, buf.data, buf.len, NG_CH_UNRELIABLE, false);
}
#endif

static bool mod_net_on_msg(const NgMsg *msg, void *vctx) {
  ModNetCtx *ctx = (ModNetCtx *)vctx;
  if (!ctx || !msg) {
    return false;
  }

#ifdef NG_SERVER
  if (msg->kind == NG_MSG_SNAPSHOT && msg->snapshot) {
    ctx->snapshot_buf = *msg->snapshot;
    ng_net_foreach_peer(ctx->net, mod_net_send_snapshot_peer, ctx);
    mod_net_send_ws_snapshot(ctx, msg->snapshot);
    return true;
  }
  if (msg->kind == NG_MSG_REPLY && msg->text) {
    NgProtoBuf buf;
    if (ng_proto_encode_text(&buf, NG_PKT_CMD_REPLY, ++ctx->seq, msg->text)) {
      ng_net_send(ctx->net, buf.data, buf.len, NG_CH_RELIABLE, true);
      ng_net_flush(ctx->net);
      if (ctx->ws) {
        ng_ws_server_send(ctx->ws, buf.data, buf.len);
      }
    }
    return true;
  }
  if (msg->kind == NG_MSG_EVENT && msg->text) {
    NgProtoBuf buf;
    if (ng_proto_encode_text(&buf, NG_PKT_EVENT, ++ctx->seq, msg->text)) {
      ng_net_send(ctx->net, buf.data, buf.len, NG_CH_RELIABLE, true);
      ng_net_flush(ctx->net);
      if (ctx->ws) {
        ng_ws_server_send(ctx->ws, buf.data, buf.len);
      }
    }
    return true;
  }
#else
  if (msg->kind == NG_MSG_CMD && msg->line) {
    NgProtoBuf buf;
    if (ng_proto_encode_cmd(&buf, ++ctx->seq, msg->line)) {
      ng_net_send(ctx->net, buf.data, buf.len, NG_CH_RELIABLE, true);
      ng_net_flush(ctx->net);
    }
    return true;
  }
  if (msg->kind == NG_MSG_TICK) {
    mod_net_send_input(ctx);
    return true;
  }
#endif
  return false;
}

static bool mod_net_init(void *vctx) {
  ModNetCtx *ctx = (ModNetCtx *)vctx;
  char host_save[64];
  const uint16_t port_save = ctx->port;
  if (ctx->host[0] != '\0') {
    strncpy(host_save, ctx->host, sizeof(host_save) - 1);
    host_save[sizeof(host_save) - 1] = '\0';
  } else {
    strncpy(host_save, NG_NET_HOST, sizeof(host_save) - 1);
  }
  memset(ctx, 0, sizeof(*ctx));
  strncpy(ctx->host, host_save, sizeof(ctx->host) - 1);
  ctx->port = port_save ? port_save : NG_NET_DEFAULT_PORT;

#ifdef NG_SERVER
  ctx->net = ng_net_create(NG_NET_ROLE_HOST, NULL, ctx->port);
  ctx->ws = ng_ws_server_create(NG_NET_WS_PORT);
  if (!ctx->net) {
    return false;
  }
  if (!ctx->ws) {
    NG_LOG_WARN("websocket server unavailable on :%u", NG_NET_WS_PORT);
  }
  ng_net_set_peer_fn(ctx->net, mod_net_on_peer, ctx);
  return true;
#else
  ctx->net = ng_net_create(NG_NET_ROLE_CLIENT, ctx->host, ctx->port);
  if (!ctx->net) {
    return false;
  }
  if (!g_connect_t0_set) {
    g_connect_t0 = GetTime();
    g_connect_t0_set = true;
  }
  return true;
#endif
}

static void mod_net_shutdown(void *vctx) {
  ModNetCtx *ctx = (ModNetCtx *)vctx;
#ifdef NG_SERVER
  ng_ws_server_destroy(ctx->ws);
  ctx->ws = NULL;
#endif
  ng_net_destroy(ctx->net);
  ctx->net = NULL;
}

static bool mod_net_tick(const NgMsg *msg, void *vctx) {
  (void)msg;
  ModNetCtx *ctx = (ModNetCtx *)vctx;
  ng_net_poll(ctx->net, mod_net_handle_packet, ctx);
#ifdef NG_SERVER
  if (ctx->ws) {
    const bool ws_up = ng_ws_server_connected(ctx->ws);
    ng_ws_server_poll(ctx->ws, mod_net_handle_ws_packet, ctx);
    if (ws_up && !ctx->ws_was_connected) {
      NgSnapshot full = {0};
      ng_world_fill_snapshot(mod_sim_world(), &full);
      ctx->snapshot_buf = full;
      ctx->have_baseline = false;
      mod_net_send_ws_snapshot(ctx, &full);
    }
    ctx->ws_was_connected = ws_up;
  }
#endif
  return true;
}

static bool mod_net_on_msg_wrap(const NgMsg *msg, void *vctx) {
  if (msg->kind == NG_MSG_TICK && msg->to == NG_BUS_ANY) {
    mod_net_tick(msg, vctx);
  }
  return mod_net_on_msg(msg, vctx);
}

static const NgModOps g_net_ops = {
    .name = "net",
    .dest = NG_BUS_NET,
    .init = mod_net_init,
    .shutdown = mod_net_shutdown,
    .on_msg = mod_net_on_msg_wrap,
};

const NgModOps *mod_net_ops(void) { return &g_net_ops; }

void *mod_net_ctx(void) { return &g_net_ctx; }
