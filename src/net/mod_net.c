// agent: composer-2.5 | 2026-07-25 | mod_net embedded dual net | d02295
#include "mod_net.h"
#include "engine/ng_action.h"
#include "engine/ng_bus.h"
#include "engine/ng_log.h"
#include "engine/ng_proto.h"
#include "engine/ng_session.h"
#if defined(NG_HAS_EMBEDDED) || !defined(NG_SERVER)
#include "client/render.h"
#endif
#include "scene/scene.h"
#include "scene/graph.h"
#include "net/ng_net.h"
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
#include "server/sim.h"
#endif
#if defined(NG_SERVER)
#include "net/ng_ws_server.h"
#endif
#include "world/ng_world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(NG_HAS_EMBEDDED)
#include "engine/ng_embed.h"
#endif

#if defined(NG_HAS_EMBEDDED) || !defined(NG_SERVER)
#include <raylib.h>
#include <unistd.h>
#endif


#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
typedef struct NetPeerState {
  NgSnapshot baseline;
  bool have_baseline;
  uint16_t seq;
  bool pending_connect_snap;
  bool pending_connect_session;
  uint8_t peer_id;
} NetPeerState;
#endif

typedef struct ModNetCtx {
  NgNet *net;
#if defined(NG_HAS_EMBEDDED)
  NgNet *net_client;
  NgNetLoopbackPair *loopback;
  bool embedded;
  bool local_loopback;
#endif
#if defined(NG_SERVER)
  NgWsServer *ws;
  bool ws_was_connected;
#endif
  char host[64];
  uint16_t port;
  uint16_t seq;
  NgSnapshot snapshot_buf;
  NgSnapshot baseline;
  NgSnapshot wire_snap;
  NgProtoBuf rx_buf;
  NgProtoBuf tx_buf;
  bool have_baseline;
  uint32_t last_snap_tick;
  bool pending_snapshot;
  bool pending_reply;
  char pending_reply_text[1024];
  uint32_t last_action_tick;
  bool cmd_inflight;
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
  uint8_t controller_id;
  uint8_t next_peer_id;
#endif
} ModNetCtx;

#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
typedef struct NetRelayCtx {
  ModNetCtx *net_ctx;
  NgNetPeer *from;
} NetRelayCtx;
#endif

// agent: composer-2.5 | 2026-07-25 | snapshot proto scratch buffers | 0009c0
static void mod_net_rx_load(ModNetCtx *ctx, const uint8_t *data, size_t len) {
  ctx->rx_buf.len = len;
  ctx->rx_buf.pos = 0;
  if (len > 0) {
    memcpy(ctx->rx_buf.data, data, len);
  }
}

// agent: composer-2.5 | 2026-07-25 | defer bus from net poll | c210df
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
// agent: composer-2.5 | 2026-07-27 | session from js registry | b5c6d7
static void mod_net_fill_session(ModNetCtx *ctx, NgSessionState *session, uint8_t your_id) {
  NgWorld *w = mod_sim_world();
  memset(session, 0, sizeof(*session));
  const char *scene_id = mod_scene_current_id();
  if (!scene_id || scene_id[0] == '\0') {
    scene_id = w->scene_id;
  }
  strncpy(session->scene_id, scene_id, sizeof(session->scene_id) - 1);
  session->tick = w->tick;
  session->controller_id = ctx->controller_id;
  session->your_id = your_id;
  mod_scene_fill_session(session);
  session->scene_sync = NG_SYNC_SERVER;
  if (session->spawn_count > 0) {
    session->scene_sync = session->spawns[0].sync;
  }
}

static void mod_net_send_session_peer(NgNet *net, NgNetPeer *peer, void *vctx) {
  ModNetCtx *ctx = (ModNetCtx *)vctx;
  NetPeerState *ps = (NetPeerState *)ng_net_peer_data(peer);
  if (!ps) {
    return;
  }
  NgSessionState session;
  mod_net_fill_session(ctx, &session, ps->peer_id);
  if (!ng_proto_encode_session(&ctx->tx_buf, ++ctx->seq, &session)) {
    return;
  }
  ng_net_send_to(net, peer, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_RELIABLE, true);
}

static void mod_net_broadcast_session(ModNetCtx *ctx, NgNet *net) {
  if (!net) {
    return;
  }
  ng_net_foreach_peer(net, mod_net_send_session_peer, ctx);
  ng_net_flush(net);
}

static void mod_net_relay_state_update(NgNet *net, NgNetPeer *peer, void *vctx) {
  NetRelayCtx *relay = (NetRelayCtx *)vctx;
  if (peer == relay->from) {
    return;
  }
  ng_net_send_to(net, peer, relay->net_ctx->tx_buf.data, relay->net_ctx->tx_buf.len,
                 NG_CH_UNRELIABLE, false);
}

static void mod_net_pick_controller(NgNet *net, NgNetPeer *peer, void *vctx) {
  ModNetCtx *ctx = (ModNetCtx *)vctx;
  (void)net;
  if (ctx->controller_id != 0) {
    return;
  }
  NetPeerState *ps = (NetPeerState *)ng_net_peer_data(peer);
  if (ps) {
    ctx->controller_id = ps->peer_id;
  }
}

static void mod_net_fill_snapshot_buf(ModNetCtx *ctx) {
  NgWorld *w = mod_sim_world();
  if (w->live_count > 32) {
    ng_world_fill_snapshot_aoi(w, &ctx->snapshot_buf, 0.0f, 0.0f, 1000.0f, w->tick);
  } else {
    ng_world_fill_snapshot(w, &ctx->snapshot_buf);
  }
}

static void mod_net_send_snapshot_peer(NgNet *net, NgNetPeer *peer, void *vctx);
#endif

#if defined(NG_HAS_EMBEDDED) || !defined(NG_SERVER)
static NgNet *mod_net_client_link(ModNetCtx *ctx);
static void mod_net_flush_client_pending(ModNetCtx *ctx);
static void mod_net_handle_client_packet(NgNet *net, NgNetPeer *peer, const uint8_t *data,
                                         size_t len, uint8_t channel, void *vctx);
#endif
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
static void mod_net_handle_host_packet(NgNet *net, NgNetPeer *peer, const uint8_t *data, size_t len,
                                       uint8_t channel, void *vctx);
#endif
#if defined(NG_HAS_EMBEDDED) || !defined(NG_SERVER)

static void mod_net_flush_client_pending(ModNetCtx *ctx) {
  if (ctx->pending_snapshot) {
    NgMsg msg = {
        .kind = NG_MSG_SNAPSHOT,
        .from = NG_BUS_NET,
        .to = NG_BUS_RENDER,
        .snapshot = &ctx->snapshot_buf,
    };
    ng_bus_publish(&msg);
    ctx->pending_snapshot = false;
  }
  if (ctx->pending_reply) {
    NgMsg msg = {
        .kind = NG_MSG_REPLY,
        .from = NG_BUS_NET,
        .to = NG_BUS_CONSOLE,
        .text = ctx->pending_reply_text,
    };
    ng_bus_publish(&msg);
    ctx->pending_reply = false;
  }
}

// agent: composer-2.5 | 2026-07-25 | same frame cmd poll recv | 51fe4b
static void mod_net_client_recv(ModNetCtx *ctx) {
#if defined(NG_HAS_EMBEDDED)
  if (ctx->embedded) {
    return;
  }
#endif
  NgNet *link = mod_net_client_link(ctx);
  if (!link) {
    return;
  }
  ng_net_poll(link, mod_net_handle_client_packet, ctx);
  mod_net_flush_client_pending(ctx);
}

// agent: composer-2.5 | 2026-07-26 | fix client cmd wait poll | a064c9
static void mod_net_client_wait_cmd(ModNetCtx *ctx) {
  if (!ctx->cmd_inflight) {
    return;
  }
#if defined(NG_HAS_EMBEDDED)
  if (ctx->local_loopback && ctx->net) {
    for (int i = 0; i < 64 && ctx->cmd_inflight; i++) {
      ng_net_poll(ctx->net, mod_net_handle_host_packet, ctx);
      mod_net_client_recv(ctx);
    }
    return;
  }
#endif
  NgNet *link = mod_net_client_link(ctx);
  const double deadline = GetTime() + 2.0;
  while (ctx->cmd_inflight && GetTime() < deadline && link) {
    ng_net_poll_wait(link, mod_net_handle_client_packet, ctx, 1);
    mod_net_flush_client_pending(ctx);
  }
}
#endif

#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
// agent: composer-2.5 | 2026-07-25 | inline action result wire send | c3d4e5
static void mod_net_send_action_result(ModNetCtx *ctx, NgNet *net, NgNetPeer *peer,
                                       const NgActionResult *result) {
  if (!ctx || !result || !ng_proto_encode_action_result(&ctx->tx_buf, result)) {
    return;
  }
  if (net && peer) {
    ng_net_send_to(net, peer, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_RELIABLE, true);
    ng_net_flush(net);
  } else if (net) {
    ng_net_send(net, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_RELIABLE, true);
    ng_net_flush(net);
  }
#if defined(NG_SERVER)
  if (ctx->ws && !peer) {
    ng_ws_server_send(ctx->ws, ctx->tx_buf.data, ctx->tx_buf.len);
  }
#endif
}

static void mod_net_exec_host_cmd(ModNetCtx *ctx, NgNet *net, NgNetPeer *peer,
                                  const char *line, uint16_t cmd_seq) {
  if (!line) {
    return;
  }
  NG_LOG_INFO("host cmd seq=%u", cmd_seq);
  if (strncmp(line, "scene ", 6) == 0) {
    NgActionResult result = {0};
    NgMsg cmd = {
        .kind = NG_MSG_CMD,
        .from = NG_BUS_NET,
        .to = NG_BUS_SIM,
        .line = line,
    };
    if (ng_action_server_exec(mod_sim_world(), &cmd, cmd_seq, &result)) {
      if (result.have_state) {
        ctx->snapshot_buf = result.state;
      } else if (strncmp(line, "scene ", 6) == 0) {
        mod_net_fill_snapshot_buf(ctx);
      }
      mod_net_send_action_result(ctx, net, peer, &result);
      if (strncmp(line, "scene ", 6) == 0) {
        mod_net_broadcast_session(ctx, net);
        ng_net_foreach_peer(net, mod_net_send_snapshot_peer, ctx);
      }
      ng_net_flush(net);
    }
    return;
  }
  NgMsg msg = {
      .kind = NG_MSG_CMD,
      .from = NG_BUS_NET,
      .to = NG_BUS_SCRIPT,
      .line = line,
  };
  ng_bus_publish(&msg);
}
#endif

#if defined(NG_HAS_EMBEDDED) || !defined(NG_SERVER)
// agent: composer-2.5 | 2026-07-25 | client inline action apply | f6a7b8
static void mod_net_client_apply_action(ModNetCtx *ctx, const NgActionResult *result) {
  if (!ctx || !result) {
    return;
  }
  mod_render_apply_action(result);
  if (result->reply[0] != '\0') {
    NgMsg reply = {
        .kind = NG_MSG_REPLY,
        .from = NG_BUS_NET,
        .to = NG_BUS_CONSOLE,
        .text = result->reply,
    };
    ng_bus_publish(&reply);
  }
  if (result->have_state) {
    ctx->baseline = result->state;
    ctx->have_baseline = true;
    ctx->last_snap_tick = result->state.tick;
  }
  ctx->last_action_tick = result->server_tick;
  ctx->pending_snapshot = false;
  ctx->cmd_inflight = false;
}
#endif

#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
static void mod_net_send_snapshot_peer(NgNet *net, NgNetPeer *peer, void *vctx);

static void mod_net_send_action_to_peer(NgNet *net, NgNetPeer *peer, void *vctx) {
  ModNetCtx *ctx = (ModNetCtx *)vctx;
  if (ctx->tx_buf.len > 0) {
    ng_net_send_to(net, peer, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_RELIABLE, true);
  }
}

// agent: composer-2.5 | 2026-07-26 | defer connect snapshot send | 3aba1f
static void mod_net_send_connect_snapshot(NgNet *net, NgNetPeer *peer, void *vctx) {
  ModNetCtx *ctx = (ModNetCtx *)vctx;
  NetPeerState *ps = (NetPeerState *)ng_net_peer_data(peer);
  if (!ps) {
    return;
  }
  if (ps->pending_connect_session) {
    ps->pending_connect_session = false;
    mod_net_send_session_peer(net, peer, ctx);
  }
  if (ps->pending_connect_snap) {
    ps->pending_connect_snap = false;
    if (ng_proto_encode_snapshot(&ctx->tx_buf, &ctx->snapshot_buf, ++ps->seq, false)) {
      ng_net_send_to(net, peer, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_RELIABLE, true);
      ps->baseline = ctx->snapshot_buf;
      ps->have_baseline = true;
    }
  }
}
#endif
#if defined(NG_SERVER)
static void mod_net_send_ws_snapshot(ModNetCtx *ctx, const NgSnapshot *full);
#endif

static ModNetCtx g_net_ctx;
#if defined(NG_HAS_EMBEDDED) || !defined(NG_SERVER)
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

#if defined(NG_HAS_EMBEDDED)
static bool g_net_embedded = false;
static bool g_net_local_loopback = false;

void mod_net_set_embedded(bool embedded) { g_net_embedded = embedded; }

void mod_net_set_local_loopback(bool local) { g_net_local_loopback = local; }

bool mod_net_is_embedded(void) { return g_net_ctx.embedded || g_net_embedded; }

bool mod_net_is_local_loopback(void) { return g_net_ctx.local_loopback || g_net_local_loopback; }
#endif

static NgNet *mod_net_client_link(ModNetCtx *ctx) {
#if defined(NG_HAS_EMBEDDED)
  if (ctx->local_loopback || ctx->embedded) {
    return ctx->net_client ? ctx->net_client : ctx->net;
  }
#endif
  return ctx->net;
}

bool mod_net_is_connected(void) {
#if defined(NG_HAS_EMBEDDED)
  if (mod_net_is_embedded()) {
    return ng_embed_ready();
  }
  if (mod_net_is_local_loopback()) {
    return g_net_ctx.loopback != NULL && g_net_ctx.net_client != NULL;
  }
#endif
  NgNet *link = mod_net_client_link(&g_net_ctx);
  return link != NULL && ng_net_connected(link);
}

#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
bool mod_net_has_clients(void) {
#if defined(NG_HAS_EMBEDDED)
  if (g_net_ctx.embedded) {
    return g_net_ctx.net != NULL && ng_net_connected(g_net_ctx.net);
  }
#endif
  if (g_net_ctx.net && ng_net_connected(g_net_ctx.net)) {
    return true;
  }
#if defined(NG_SERVER)
  if (g_net_ctx.ws && ng_ws_server_connected(g_net_ctx.ws)) {
    return true;
  }
#endif
  return false;
}
#endif

void mod_net_endpoint(char *host, size_t host_cap, uint16_t *port) {
  if (host && host_cap > 0) {
    strncpy(host, g_net_ctx.host, host_cap - 1);
    host[host_cap - 1] = '\0';
  }
  if (port) {
    *port = g_net_ctx.port;
  }
}

#if defined(NG_HAS_EMBEDDED) || !defined(NG_SERVER)
double mod_net_connect_elapsed(void) {
  if (!g_connect_t0_set) {
    return 0.0;
  }
  return GetTime() - g_connect_t0;
}
#endif

#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
static void mod_net_handle_host_packet(NgNet *net, NgNetPeer *peer, const uint8_t *data,
                                       size_t len, uint8_t channel, void *vctx) {
  ModNetCtx *ctx = (ModNetCtx *)vctx;
  (void)net;
  (void)peer;
  (void)channel;
  if (!data || len < sizeof(NgProtoHeader)) {
    return;
  }

  NgProtoBuf *buf = &ctx->rx_buf;
  mod_net_rx_load(ctx, data, len);
  buf->pos = sizeof(NgProtoHeader);

  NgProtoHeader h;
  buf->pos = 0;
  if (!ng_proto_read_header(buf, &h) || h.magic != NG_PROTO_MAGIC) {
    return;
  }

  switch (h.type) {
  case NG_PKT_INPUT: {
    (void)ctx;
    break;
  }
  case NG_PKT_CMD: {
    char line[256];
    if (!ng_proto_decode_cmd(buf, line, sizeof(line))) {
      return;
    }
    mod_net_exec_host_cmd(ctx, net, peer, line, h.seq);
    break;
  }
  case NG_PKT_STATE_UPDATE: {
    NgStateUpdate update = {.tick = h.tick};
    if (!ng_proto_decode_state_update(buf, &update)) {
      return;
    }
    const NgSyncMode sync = mod_scene_graph_sync_for_entity(update.entity_id);
    if (sync == NG_SYNC_OWNER) {
      NetPeerState *ps = (NetPeerState *)ng_net_peer_data(peer);
      if (!ps || ps->peer_id != ctx->controller_id) {
        return;
      }
    } else if (sync != NG_SYNC_SHARED) {
      return;
    }
    mod_scene_apply_remote(&update);
    if (!ng_proto_encode_state_update(&ctx->tx_buf, ++ctx->seq, &update)) {
      return;
    }
    NetRelayCtx relay = {.net_ctx = ctx, .from = peer};
    ng_net_foreach_peer(net, mod_net_relay_state_update, &relay);
    break;
  }
  case NG_PKT_STATE_BATCH: {
    NgStateUpdate updates[16];
    int count = 0;
    if (!ng_proto_decode_state_batch(buf, updates, 16, &count)) {
      return;
    }
    for (int i = 0; i < count; i++) {
      updates[i].tick = h.tick;
      const NgSyncMode sync = mod_scene_graph_sync_for_entity(updates[i].entity_id);
      if (sync == NG_SYNC_OWNER) {
        NetPeerState *ps = (NetPeerState *)ng_net_peer_data(peer);
        if (!ps || ps->peer_id != ctx->controller_id) {
          continue;
        }
      } else if (sync != NG_SYNC_SHARED) {
        continue;
      }
      mod_scene_apply_remote(&updates[i]);
    }
    if (!ng_proto_encode_state_batch(&ctx->tx_buf, ++ctx->seq, h.tick, updates, count)) {
      return;
    }
    NetRelayCtx relay = {.net_ctx = ctx, .from = peer};
    ng_net_foreach_peer(net, mod_net_relay_state_update, &relay);
    break;
  }
  case NG_PKT_STATE_ACK: {
    // agent: composer-2.5 | 2026-07-28 | host accepts state ack pkt | 2c89bf
    uint32_t entity_id = 0;
    uint16_t ack_seq = 0;
    if (ng_proto_decode_state_ack(buf, &entity_id, &ack_seq)) {
      (void)entity_id;
      (void)ack_seq;
    }
    break;
  }
  default:
    break;
  }
}
#endif

#if defined(NG_HAS_EMBEDDED) || !defined(NG_SERVER)
// agent: composer-2.5 | 2026-07-28 | client state ack side channel | 44151b
static void mod_net_send_state_ack(ModNetCtx *ctx, const NgStateUpdate *update) {
  if (!ctx || !update || update->seq == 0) {
    return;
  }
#if defined(NG_HAS_EMBEDDED)
  if (mod_net_is_embedded()) {
    return;
  }
#endif
  NgNet *link = mod_net_client_link(ctx);
  if (!ng_net_connected(link)) {
    return;
  }
  if (!ng_proto_encode_state_ack(&ctx->tx_buf, ++ctx->seq, update->entity_id, update->seq)) {
    return;
  }
  ng_net_send(link, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_UNRELIABLE, false);
}

static void mod_net_handle_client_packet(NgNet *net, NgNetPeer *peer, const uint8_t *data,
                                         size_t len, uint8_t channel, void *vctx) {
  ModNetCtx *ctx = (ModNetCtx *)vctx;
  (void)net;
  (void)peer;
  (void)channel;
  if (!data || len < sizeof(NgProtoHeader)) {
    return;
  }

  NgProtoBuf *buf = &ctx->rx_buf;
  mod_net_rx_load(ctx, data, len);
  buf->pos = sizeof(NgProtoHeader);

  NgProtoHeader h;
  buf->pos = 0;
  if (!ng_proto_read_header(buf, &h) || h.magic != NG_PROTO_MAGIC) {
    return;
  }

  switch (h.type) {
  case NG_PKT_SNAPSHOT: {
    if (ctx->last_action_tick > 0 && h.tick < ctx->last_action_tick) {
      return;
    }
    bool delta = false;
    if (ctx->have_baseline) {
      ctx->snapshot_buf = ctx->baseline;
    } else {
      memset(&ctx->snapshot_buf, 0, sizeof(ctx->snapshot_buf));
    }
    if (!ng_proto_decode_snapshot(buf, &ctx->snapshot_buf, &delta)) {
      return;
    }
    ctx->baseline = ctx->snapshot_buf;
    ctx->have_baseline = true;
    ctx->last_snap_tick = ctx->snapshot_buf.tick;
#ifndef NG_SERVER
    // agent: composer-2.5 | 2026-07-28 | bootstrap scene from snapshot client | 8a3951
    if (!mod_scene_is_loaded() ||
        strcmp(mod_scene_current_id(), ctx->snapshot_buf.scene_id) != 0) {
      NgSessionState bootstrap = {0};
      strncpy(bootstrap.scene_id, ctx->snapshot_buf.scene_id, sizeof(bootstrap.scene_id) - 1);
      bootstrap.tick = ctx->snapshot_buf.tick;
      mod_scene_on_session(&bootstrap);
    }
#endif
    mod_scene_apply_snapshot(&ctx->snapshot_buf);
    ctx->pending_snapshot = true;
    break;
  }
  case NG_PKT_ACTION_RESULT: {
    NgActionResult result = {0};
    if (!ng_proto_decode_action_result(buf, &result)) {
      return;
    }
    mod_net_client_apply_action(ctx, &result);
    break;
  }
  case NG_PKT_CMD_REPLY: {
    char text[1024];
    if (!ng_proto_decode_text(buf, text, sizeof(text))) {
      return;
    }
    ctx->pending_reply = true;
    strncpy(ctx->pending_reply_text, text, sizeof(ctx->pending_reply_text) - 1);
    ctx->pending_reply_text[sizeof(ctx->pending_reply_text) - 1] = '\0';
    ctx->cmd_inflight = false;
    break;
  }
  case NG_PKT_SESSION: {
    NgSessionState session = {.tick = h.tick};
    if (!ng_proto_decode_session(buf, &session)) {
      return;
    }
    session.tick = h.tick;
    mod_scene_on_session(&session);
    break;
  }
  case NG_PKT_STATE_UPDATE: {
    NgStateUpdate update = {.tick = h.tick};
    if (!ng_proto_decode_state_update(buf, &update)) {
      return;
    }
    mod_scene_apply_remote(&update);
    mod_net_send_state_ack(ctx, &update);
    break;
  }
  case NG_PKT_STATE_BATCH: {
    NgStateUpdate updates[16];
    int count = 0;
    if (!ng_proto_decode_state_batch(buf, updates, 16, &count)) {
      return;
    }
    for (int i = 0; i < count; i++) {
      updates[i].tick = h.tick;
      mod_scene_apply_remote(&updates[i]);
      mod_net_send_state_ack(ctx, &updates[i]);
    }
    break;
  }
  default:
    break;
  }
}
#endif

#if defined(NG_SERVER)
static void mod_net_handle_packet(NgNet *net, NgNetPeer *peer, const uint8_t *data, size_t len,
                                  uint8_t channel, void *vctx) {
  mod_net_handle_host_packet(net, peer, data, len, channel, vctx);
}
#endif

#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
static void mod_net_on_peer(NgNet *net, NgNetPeer *peer, bool connected, void *vctx) {
  ModNetCtx *ctx = (ModNetCtx *)vctx;
  if (connected) {
    NetPeerState *ps = (NetPeerState *)calloc(1, sizeof(NetPeerState));
    ps->peer_id = ++ctx->next_peer_id;
    if (ctx->controller_id == 0) {
      ctx->controller_id = ps->peer_id;
    }
    ng_net_peer_set_data(peer, ps);
    mod_net_fill_snapshot_buf(ctx);
    ps->pending_connect_snap = true;
    ps->pending_connect_session = true;
  } else {
    NetPeerState *ps = (NetPeerState *)ng_net_peer_data(peer);
    const bool was_controller = ps && ps->peer_id == ctx->controller_id;
    free(ps);
    ng_net_peer_set_data(peer, NULL);
    if (was_controller) {
      ctx->controller_id = 0;
      ng_net_foreach_peer(net, mod_net_pick_controller, ctx);
      mod_net_broadcast_session(ctx, net);
    }
  }
}

static void mod_net_send_snapshot_peer(NgNet *net, NgNetPeer *peer, void *vctx) {
  ModNetCtx *ctx = (ModNetCtx *)vctx;
  const NgSnapshot *full = &ctx->snapshot_buf;
  NetPeerState *ps = (NetPeerState *)ng_net_peer_data(peer);
  if (!ps) {
    return;
  }

  NgSnapshot *wire = &ctx->wire_snap;
  const NgSnapshot *send = full;
  bool delta = false;
  if (ps->have_baseline && strcmp(ps->baseline.scene_id, full->scene_id) != 0) {
    send = full;
    delta = false;
  } else if (ps->have_baseline && ps->baseline.tick > 0) {
    ng_world_fill_snapshot_delta(mod_sim_world(), (NgSnapshot *)full, &ps->baseline, wire);
    if (wire->entity_count == 0) {
      return;
    }
    send = wire;
    delta = true;
  }

  if (!ng_proto_encode_snapshot(&ctx->tx_buf, send, ++ps->seq, delta)) {
    return;
  }
  ng_net_send_to(net, peer, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_UNRELIABLE, false);
  ps->baseline = *full;
  ps->have_baseline = true;
}
#endif

#if defined(NG_SERVER)
static void mod_net_send_ws_snapshot(ModNetCtx *ctx, const NgSnapshot *full) {
  if (!ctx->ws || !full) {
    return;
  }
  NgSnapshot *wire = &ctx->wire_snap;
  const NgSnapshot *send = full;
  bool delta = false;
  if (ctx->have_baseline && strcmp(ctx->baseline.scene_id, full->scene_id) != 0) {
    send = full;
    delta = false;
  } else if (ctx->have_baseline && ctx->baseline.tick > 0) {
    ng_world_fill_snapshot_delta(mod_sim_world(), (NgSnapshot *)full, &ctx->baseline, wire);
    if (wire->entity_count == 0) {
      return;
    }
    send = wire;
    delta = true;
  }
  if (!ng_proto_encode_snapshot(&ctx->tx_buf, send, ++ctx->seq, delta)) {
    return;
  }
  ng_ws_server_send(ctx->ws, ctx->tx_buf.data, ctx->tx_buf.len);
  ctx->baseline = *full;
  ctx->have_baseline = true;
}

static void mod_net_handle_ws_packet(const uint8_t *data, size_t len, void *vctx) {
  mod_net_handle_host_packet(NULL, NULL, data, len, NG_CH_RELIABLE, vctx);
}
#endif

static bool mod_net_on_msg(const NgMsg *msg, void *vctx) {
  ModNetCtx *ctx = (ModNetCtx *)vctx;
  if (!ctx || !msg) {
    return false;
  }

#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
  if (msg->kind == NG_MSG_SNAPSHOT && msg->snapshot) {
    ctx->snapshot_buf = *msg->snapshot;
    ng_net_foreach_peer(ctx->net, mod_net_send_snapshot_peer, ctx);
#if defined(NG_SERVER)
    mod_net_send_ws_snapshot(ctx, msg->snapshot);
#endif
    return true;
  }
  if (msg->kind == NG_MSG_ACTION_RESULT && msg->action_result) {
    if (ng_proto_encode_action_result(&ctx->tx_buf, msg->action_result)) {
      ng_net_foreach_peer(ctx->net, mod_net_send_action_to_peer, ctx);
      ng_net_flush(ctx->net);
#if defined(NG_SERVER)
      if (ctx->ws) {
        ng_ws_server_send(ctx->ws, ctx->tx_buf.data, ctx->tx_buf.len);
      }
#endif
    }
    return true;
  }
  if (msg->kind == NG_MSG_REPLY && msg->text) {
    if (ng_proto_encode_text(&ctx->tx_buf, NG_PKT_CMD_REPLY, ++ctx->seq, msg->text)) {
      ng_net_send(ctx->net, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_RELIABLE, true);
      ng_net_flush(ctx->net);
#if defined(NG_SERVER)
      if (ctx->ws) {
        ng_ws_server_send(ctx->ws, ctx->tx_buf.data, ctx->tx_buf.len);
      }
#endif
    }
    return true;
  }
  if (msg->kind == NG_MSG_EVENT && msg->text) {
    if (ng_proto_encode_text(&ctx->tx_buf, NG_PKT_EVENT, ++ctx->seq, msg->text)) {
      ng_net_send(ctx->net, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_RELIABLE, true);
      ng_net_flush(ctx->net);
#if defined(NG_SERVER)
      if (ctx->ws) {
        ng_ws_server_send(ctx->ws, ctx->tx_buf.data, ctx->tx_buf.len);
      }
#endif
    }
    return true;
  }
#endif

#if defined(NG_HAS_EMBEDDED) || !defined(NG_SERVER)
  if (msg->kind == NG_MSG_CMD && msg->line) {
    if (ctx->embedded) {
      NgBusDest dest = NG_BUS_SCRIPT;
      if (strncmp(msg->line, "scene ", 6) == 0) {
        dest = NG_BUS_SIM;
      }
      NgMsg fwd = {
          .kind = NG_MSG_CMD,
          .from = NG_BUS_NET,
          .to = dest,
          .line = msg->line,
      };
      ng_bus_publish(&fwd);
      return true;
    }
#if defined(NG_HAS_EMBEDDED)
    // agent: composer-2.5 | 2026-07-26 | inline loopback cmd exec | 2b36e4
    if (ctx->local_loopback && ctx->net) {
      ctx->cmd_inflight = true;
      mod_net_exec_host_cmd(ctx, ctx->net, NULL, msg->line, ++ctx->seq);
      mod_net_client_recv(ctx);
      return true;
    }
#endif
    NgNet *link = mod_net_client_link(ctx);
    if (ng_proto_encode_cmd(&ctx->tx_buf, ++ctx->seq, msg->line)) {
      ctx->cmd_inflight = true;
      ng_net_send(link, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_RELIABLE, true);
      ng_net_flush(link);
      mod_net_client_wait_cmd(ctx);
    }
    return true;
  }
  if (msg->kind == NG_MSG_TICK) {
#if defined(NG_HAS_EMBEDDED)
    if (ctx->embedded) {
      return true;
    }
#endif
    return true;
  }
#endif
  return false;
}

static bool mod_net_init(void *vctx) {
  ModNetCtx *ctx = (ModNetCtx *)vctx;
  char host_save[64];
  const uint16_t port_save = ctx->port;
#if defined(NG_HAS_EMBEDDED)
  const bool embedded_save = g_net_embedded;
  const bool local_save = g_net_local_loopback;
#else
  const bool embedded_save = false;
  const bool local_save = false;
#endif
  if (ctx->host[0] != '\0') {
    strncpy(host_save, ctx->host, sizeof(host_save) - 1);
    host_save[sizeof(host_save) - 1] = '\0';
  } else {
    strncpy(host_save, NG_NET_HOST, sizeof(host_save) - 1);
  }
  memset(ctx, 0, sizeof(*ctx));
  strncpy(ctx->host, host_save, sizeof(ctx->host) - 1);
  ctx->port = port_save ? port_save : NG_NET_DEFAULT_PORT;
#if defined(NG_HAS_EMBEDDED)
  ctx->embedded = embedded_save;
  ctx->local_loopback = local_save;
#endif

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
#elif defined(NG_HAS_EMBEDDED)
  if (ctx->embedded) {
    return true;
  }
  if (ctx->local_loopback) {
    ctx->loopback = ng_net_loopback_create();
    if (!ctx->loopback) {
      return false;
    }
    ctx->net = ng_net_loopback_host(ctx->loopback);
    ctx->net_client = ng_net_loopback_client(ctx->loopback);
    ng_net_set_peer_fn(ctx->net, mod_net_on_peer, ctx);
    ng_net_loopback_connect(ctx->loopback);
    if (!g_connect_t0_set) {
      g_connect_t0 = GetTime();
      g_connect_t0_set = true;
    }
    return true;
  }
  ctx->net = ng_net_create(NG_NET_ROLE_CLIENT, ctx->host, ctx->port);
  if (!ctx->net) {
    return false;
  }
  if (!g_connect_t0_set) {
    g_connect_t0 = GetTime();
    g_connect_t0_set = true;
  }
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
#if defined(NG_SERVER)
  ng_ws_server_destroy(ctx->ws);
  ctx->ws = NULL;
#endif
#if defined(NG_HAS_EMBEDDED)
  if (ctx->loopback) {
    ng_net_loopback_destroy(ctx->loopback);
    ctx->loopback = NULL;
    ctx->net = NULL;
    ctx->net_client = NULL;
    return;
  }
#endif
  ng_net_destroy(ctx->net);
  ctx->net = NULL;
}

static bool mod_net_tick(const NgMsg *msg, void *vctx) {
  (void)msg;
  ModNetCtx *ctx = (ModNetCtx *)vctx;
#if defined(NG_HAS_EMBEDDED)
  if (ctx->embedded) {
    return true;
  }
  if (ctx->local_loopback) {
    ng_net_poll(ctx->net, mod_net_handle_host_packet, ctx);
    ng_net_foreach_peer(ctx->net, mod_net_send_connect_snapshot, ctx);
    mod_net_client_recv(ctx);
    return true;
  }
#endif
#if defined(NG_SERVER)
  ng_net_poll(ctx->net, mod_net_handle_packet, ctx);
  if (ctx->ws) {
    const bool ws_up = ng_ws_server_connected(ctx->ws);
    ng_ws_server_poll(ctx->ws, mod_net_handle_ws_packet, ctx);
    if (ws_up && !ctx->ws_was_connected) {
      mod_net_fill_snapshot_buf(ctx);
      ctx->have_baseline = false;
      mod_net_send_ws_snapshot(ctx, &ctx->snapshot_buf);
    }
    ctx->ws_was_connected = ws_up;
  }
#else
  mod_net_client_recv(ctx);
#endif
  return true;
}

#if defined(NG_SERVER)
void mod_net_server_poll(void) {
  ModNetCtx *ctx = &g_net_ctx;
  if (!ctx->net) {
    return;
  }
  ng_net_poll(ctx->net, mod_net_handle_packet, ctx);
  ng_net_foreach_peer(ctx->net, mod_net_send_connect_snapshot, ctx);
  ng_net_flush(ctx->net);
  if (ctx->ws) {
    ng_ws_server_poll(ctx->ws, mod_net_handle_ws_packet, ctx);
  }
}
#endif

#if defined(NG_HAS_EMBEDDED) || !defined(NG_SERVER)
void mod_net_poll_recv(void) {
  ModNetCtx *ctx = &g_net_ctx;
#if defined(NG_HAS_EMBEDDED)
  if (ctx->local_loopback && ctx->net) {
    ng_net_poll(ctx->net, mod_net_handle_host_packet, ctx);
  }
#endif
  mod_net_client_recv(ctx);
}
#endif

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

#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
static void mod_net_send_state_peer(NgNet *net, NgNetPeer *peer, void *vctx) {
  ModNetCtx *ctx = (ModNetCtx *)vctx;
  ng_net_send_to(net, peer, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_UNRELIABLE, false);
}
#endif

static void mod_net_flush_state_update(ModNetCtx *ctx) {
  if (!mod_scene_is_loaded()) {
    return;
  }
#if defined(NG_HAS_EMBEDDED)
  if (mod_net_is_embedded()) {
    return;
  }
#endif
  NgStateUpdate batch[16];
  for (;;) {
    int count = 0;
    uint32_t tick = 0;
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
    tick = mod_sim_world()->tick;
#else
    tick = ctx->last_snap_tick;
#endif
    while (count < 16 && mod_scene_take_flush(&batch[count])) {
      batch[count].tick = tick;
      count++;
    }
    if (count == 0) {
      return;
    }
    const bool ok =
        (count == 1)
            ? ng_proto_encode_state_update(&ctx->tx_buf, ++ctx->seq, &batch[0])
            : ng_proto_encode_state_batch(&ctx->tx_buf, ++ctx->seq, tick, batch, count);
    if (!ok) {
      return;
    }
#if defined(NG_SERVER)
    if (ctx->net) {
      ng_net_foreach_peer(ctx->net, mod_net_send_state_peer, ctx);
      ng_net_flush(ctx->net);
    }
#elif defined(NG_HAS_EMBEDDED) || !defined(NG_SERVER)
    NgNet *link = mod_net_client_link(ctx);
    if (ng_net_connected(link)) {
      ng_net_send(link, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_UNRELIABLE, false);
    }
#endif
  }
}

void mod_net_flush_scene_updates(void) { mod_net_flush_state_update(&g_net_ctx); }

#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
void mod_net_broadcast_scene_session(void) {
  ModNetCtx *ctx = &g_net_ctx;
  if (ctx->net) {
    mod_net_broadcast_session(ctx, ctx->net);
  }
}
#endif

const NgModOps *mod_net_ops(void) { return &g_net_ops; }

void *mod_net_ctx(void) { return &g_net_ctx; }

// agent: composer-2.5 | 2026-07-28 | session before snapshot on connect | d364f3
// agent: composer-2.5 | 2026-07-28 | bootstrap scene from snapshot client | 8a3951
