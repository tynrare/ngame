// agent: composer-2.5 | 2026-07-25 | mod_net embedded dual net | d02295
// agent: composer-2.5 | 2026-07-28 | gateway loopback upstream API | 34cffe
// agent: composer-2.5 | 2026-07-30 | net late join sync flow | 0f03f2
// agent: composer-2.5 | 2026-07-30 | lockstep join fixes logging | 8c64cd
// agent: composer-2.5 | 2026-07-30 | hash mismatch stalls sync | 10e625
// agent: composer-2.5 | 2026-07-30 | remove peer on disconnect | cbe0b7
// agent: composer-2.5 | 2026-07-30 | join ready mismatch aborts | 32918c
// agent: composer-2.5 | 2026-07-30 | lockstep session server load | f8de1b
// agent: composer-2.5 | 2026-07-30 | broadcast synth lock inputs | 96e06d
// agent: composer-2.5 | 2026-07-30 | lockstep peer roster sync | ffd508
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
#include "scene/runtime.h"
#include "scene/graph.h"
#include "scene/lockstep.h"
#include "scene/physics.h"
#include "net/ng_net.h"
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
#include "server/sim.h"
#endif
#if defined(NG_SERVER)
#include "net/ng_ws_server.h"
#endif
#include "world/ng_world.h"
#include "box3d/box3d.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  bool pending_lock_phys;
  uint8_t *lock_phys_data;
  int lock_phys_size;
  int lock_phys_sent;
  uint32_t lock_phys_tick;
  uint8_t peer_id;
  uint8_t role;
  uint16_t assigned_agent_port;
  char name[32];
  bool registered;
} NetPeerState;

typedef struct NgRootMirror {
  char scene_id[32];
  uint32_t tick;
  int entity_count;
  bool valid;
} NgRootMirror;

static NgRootMirror g_root_mirror;
static uint16_t g_next_dependent_agent_port = 27101;
#endif

typedef struct ModNetCtx {
  NgNet *net;
#if defined(NG_HAS_EMBEDDED)
  NgNet *net_client;
  NgNet *net_upstream;
  NgNetLoopbackPair *loopback;
  bool gateway;
  char upstream_host[64];
  uint16_t upstream_port;
  bool upstream_pending_reply;
  char upstream_reply[1024];
  uint16_t assigned_agent_port;
  bool register_pending;
  bool console_cmd_pending;
  // agent: composer-2.5 | 2026-07-30 | async upstream connect status | 421223
  int upstream_phase; /* 0 none, 1 connecting, 2 registering, 3 waiting scene, 4 ready, 5 lost */
  int upstream_phase_logged;
  bool upstream_register_sent;
  bool upstream_was_connected;
  double upstream_phase_t0;
  double upstream_last_hint_log;
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
  uint8_t lock_peer_id;
  uint32_t lock_hash_sent_tick;
  uint32_t lock_roster_sent_tick;
  uint32_t lock_roster_pulse; /* wall-ish flush count for cold-start roster spam */
  uint32_t lock_last_sim_tick; /* detect lockstep clock restart (scene reload) */
  /* Bumped only in broadcast_scene_session; carried in snap_tick when !syncing
   * so clients force-reload same scene ids. Controller SESSION leaves snap=0. */
  // agent: cursor-grok-4.5 | 2026-07-31 | scene gen only on load | cebf8b
  uint32_t scene_gen;
  uint32_t applied_scene_gen;
  bool session_carry_gen;
  uint8_t *lock_phys_rx;
  int lock_phys_rx_size;
  int lock_phys_rx_got;
  uint32_t lock_phys_rx_tick;
  uint8_t lock_joining_peer;
  uint32_t lock_join_hash;
  bool lock_join_pending;
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
static void mod_net_update_root_mirror(const NgSnapshot *snap) {
  if (!snap) {
    return;
  }
  strncpy(g_root_mirror.scene_id, snap->scene_id, sizeof(g_root_mirror.scene_id) - 1);
  g_root_mirror.scene_id[sizeof(g_root_mirror.scene_id) - 1] = '\0';
  g_root_mirror.tick = snap->tick;
  g_root_mirror.entity_count = snap->entity_count;
  g_root_mirror.valid = true;
}

// agent: composer-2.5 | 2026-07-29 | root mirror tracks sessions | 5809c3
static void mod_net_update_root_mirror_session(const NgSessionState *session) {
  if (!session || session->scene_id[0] == '\0') {
    return;
  }
  strncpy(g_root_mirror.scene_id, session->scene_id, sizeof(g_root_mirror.scene_id) - 1);
  g_root_mirror.scene_id[sizeof(g_root_mirror.scene_id) - 1] = '\0';
  g_root_mirror.tick = session->tick;
  g_root_mirror.valid = true;
}

static uint16_t mod_net_alloc_agent_port(void) { return g_next_dependent_agent_port++; }

static void mod_net_fill_session(ModNetCtx *ctx, NgSessionState *session, uint8_t your_id) {
  mod_scene_runtime_use_server();
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
  if (mod_lockstep_syncing() || ctx->lock_join_pending) {
    session->syncing = 1u;
    session->snap_tick = mod_lockstep_sim_tick();
  } else if (ctx->session_carry_gen && ctx->scene_gen != 0u) {
    /* Scene load only — solar uses session.syncing for join, not snap_tick. */
    // agent: cursor-grok-4.5 | 2026-07-31 | scene gen only on load | cebf8b
    session->snap_tick = ctx->scene_gen;
  }
}


static void mod_net_lock_free_peer_phys(NetPeerState *ps) {
  if (ps && ps->lock_phys_data) {
    // agent: cursor-grok-4.5 | 2026-07-31 | phys joiner only no fanout | c01e05
    b3FreeSaveData(ps->lock_phys_data, ps->lock_phys_size);
    ps->lock_phys_data = NULL;
    ps->lock_phys_size = 0;
    ps->lock_phys_sent = 0;
    ps->pending_lock_phys = false;
  }
}

static void mod_net_lock_free_rx(ModNetCtx *ctx) {
  if (ctx->lock_phys_rx) {
    free(ctx->lock_phys_rx);
    ctx->lock_phys_rx = NULL;
  }
  ctx->lock_phys_rx_size = 0;
  ctx->lock_phys_rx_got = 0;
  ctx->lock_phys_rx_tick = 0;
}

static void mod_net_broadcast_lock_pause(ModNetCtx *ctx, NgNet *net, uint32_t tick) {
  NgLockPausePkt pkt = {.sim_tick = tick};
  if (!ng_proto_encode_lock_pause(&ctx->tx_buf, ++ctx->seq, &pkt)) {
    return;
  }
  ng_net_broadcast(net, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_RELIABLE, true);
}

static void mod_net_send_lock_phys_chunks(NgNet *net, NgNetPeer *peer, ModNetCtx *ctx, NetPeerState *ps) {
  if (!ps || !ps->pending_lock_phys || !ps->lock_phys_data || ps->lock_phys_size <= 0) {
    return;
  }
  while (ps->lock_phys_sent < ps->lock_phys_size) {
    NgLockPhysPkt pkt = {0};
    pkt.sim_tick = ps->lock_phys_tick;
    pkt.offset = (uint32_t)ps->lock_phys_sent;
    pkt.total = (uint32_t)ps->lock_phys_size;
    uint32_t remain = (uint32_t)(ps->lock_phys_size - ps->lock_phys_sent);
    pkt.len = (uint16_t)(remain > NG_LOCK_PHYS_CHUNK ? NG_LOCK_PHYS_CHUNK : remain);
    memcpy(pkt.data, ps->lock_phys_data + ps->lock_phys_sent, pkt.len);
    if (!ng_proto_encode_lock_phys(&ctx->tx_buf, ++ctx->seq, &pkt)) {
      break;
    }
    ng_net_send_to(net, peer, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_RELIABLE, true);
    ps->lock_phys_sent += (int)pkt.len;
  }
  if (ps->lock_phys_sent >= ps->lock_phys_size) {
    mod_net_lock_free_peer_phys(ps);
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

// agent: composer-2.5 | 2026-07-30 | lockstep peer roster sync | ffd508
static void mod_net_lockstep_add_peer_cb(NgNet *net, NgNetPeer *peer, void *vctx) {
  (void)net;
  (void)vctx;
  NetPeerState *ps = (NetPeerState *)ng_net_peer_data(peer);
  if (ps && ps->peer_id != 0) {
    mod_lockstep_add_peer(ps->peer_id);
  }
}

static void mod_net_roster_acc_cb(NgNet *net, NgNetPeer *peer, void *vctx) {
  (void)net;
  NgLockResumePkt *resume = (NgLockResumePkt *)vctx;
  NetPeerState *ps = (NetPeerState *)ng_net_peer_data(peer);
  if (!ps || ps->peer_id == 0 || resume->peer_count >= NG_LOCK_PEER_MAX) {
    return;
  }
  for (uint8_t i = 0; i < resume->peer_count; i++) {
    if (resume->peer_ids[i] == (uint8_t)ps->peer_id) {
      return;
    }
  }
  resume->peer_ids[resume->peer_count++] = (uint8_t)ps->peer_id;
}

static void mod_net_count_peers_cb(NgNet *net, NgNetPeer *peer, void *vctx) {
  (void)net;
  (void)peer;
  int *n = (int *)vctx;
  if (n) {
    (*n)++;
  }
}

/* Clock owner: register every connected net peer and broadcast the roster. */
static void mod_net_lockstep_broadcast_roster(ModNetCtx *ctx, NgNet *net) {
  if (!ctx || !net || !mod_lockstep_active() || !mod_lockstep_is_clock_owner()) {
    return;
  }
  if (mod_lockstep_syncing() || ctx->lock_join_pending) {
    return;
  }
  ng_net_foreach_peer(net, mod_net_lockstep_add_peer_cb, ctx);
  NgLockResumePkt resume = {0};
  resume.sim_tick = mod_lockstep_sim_tick();
  resume.peer_count = 0;
  uint32_t local = mod_lockstep_local_peer_id();
  if (local) {
    resume.peer_ids[resume.peer_count++] = (uint8_t)local;
  }
  ng_net_foreach_peer(net, mod_net_roster_acc_cb, &resume);
  if (resume.peer_count == 0) {
    return;
  }
  if (!ng_proto_encode_lock_resume(&ctx->tx_buf, ++ctx->seq, &resume)) {
    return;
  }
  NG_LOG_INFO("lockstep: roster RESUME tick=%u peers=%u", resume.sim_tick, resume.peer_count);
  ng_net_broadcast(net, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_RELIABLE, true);
  ng_net_flush(net);
}

static void mod_net_broadcast_session(ModNetCtx *ctx, NgNet *net) {
  // agent: cursor-grok-4.5 | 2026-07-31 | throttle roster outside flush | 600f58
  if (!net) {
    return;
  }
  ng_net_foreach_peer(net, mod_net_send_session_peer, ctx);
  ng_net_flush(net);
  /* One roster pulse with SESSION is enough for cold mirrors. Skip during
   * late-join pause (READY sends RESUME). Never spam reliable RESUME here. */
  if (mod_lockstep_active() && mod_lockstep_is_clock_owner() && !ctx->lock_join_pending) {
    mod_net_lockstep_broadcast_roster(ctx, net);
    ctx->lock_roster_sent_tick = mod_lockstep_sim_tick() ? mod_lockstep_sim_tick() : 1u;
    ctx->lock_roster_pulse = 1u;
    ctx->lock_last_sim_tick = mod_lockstep_sim_tick();
  }
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
#if defined(NG_HAS_EMBEDDED)
static void mod_net_handle_upstream_packet(NgNet *net, NgNetPeer *peer, const uint8_t *data,
                                           size_t len, uint8_t channel, void *vctx);
static void mod_net_forward_upstream(ModNetCtx *ctx, const uint8_t *data, size_t len,
                                     uint8_t channel, bool reliable);
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
  if (ctx->gateway && ctx->net) {
    for (int i = 0; i < 64 && ctx->cmd_inflight; i++) {
      ng_net_poll(ctx->net, mod_net_handle_host_packet, ctx);
      if (ctx->net_upstream) {
        ng_net_poll(ctx->net_upstream, mod_net_handle_upstream_packet, ctx);
      }
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
        result.have_state = true;
        result.state = ctx->snapshot_buf;
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
// agent: composer-2.5 | 2026-07-29 | sync view from snapshot | 4e8a2b
static void mod_net_sync_view_scene(const NgSnapshot *snap) {
  if (!snap || snap->scene_id[0] == '\0') {
    return;
  }
  const char *view_id = mod_scene_view_current_id();
  const bool view_loaded = mod_scene_view_is_loaded();
  if (!view_loaded || !view_id || view_id[0] == '\0') {
    NgSessionState bootstrap = {0};
    strncpy(bootstrap.scene_id, snap->scene_id, sizeof(bootstrap.scene_id) - 1);
    bootstrap.tick = snap->tick;
    mod_scene_view_on_session(&bootstrap);
    mod_scene_view_apply_snapshot(snap);
    return;
  }
  if (strcmp(view_id, snap->scene_id) != 0) {
    return;
  }
  mod_scene_view_apply_snapshot(snap);
}

static void mod_net_client_apply_action(ModNetCtx *ctx, const NgActionResult *result) {
  if (!ctx || !result) {
    return;
  }
  mod_render_apply_action(result);
#ifndef NG_SERVER
  if (result->have_state) {
    mod_net_sync_view_scene(&result->state);
  }
#endif
  if (result->reply[0] != '\0' && !ctx->console_cmd_pending && !ctx->upstream_pending_reply) {
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
  if (!ctx->upstream_pending_reply) {
    ctx->cmd_inflight = false;
  }
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
#if defined(NG_HAS_EMBEDDED)
  /* Upstream boot: suppress cold entity snap for loopback, but never drop late-join phys. */
  if (mod_net_skip_local_boot() && !ps->pending_lock_phys) {
    ps->pending_connect_session = false;
    ps->pending_connect_snap = false;
    return;
  }
#endif
  mod_scene_runtime_use_server();
  if (!mod_scene_is_loaded()) {
    // agent: cursor-grok-4.5 | 2026-07-31 | net connect log rename | af466e
    NG_LOG_INFO("net: connect defer peer=%u (scene not loaded)", ps->peer_id);
    return;
  }
  if (ps->pending_connect_session) {
    ps->pending_connect_session = false;
    // agent: cursor-grok-4.5 | 2026-07-31 | net connect log rename | af466e
    NG_LOG_INFO("net: send SESSION peer=%u join_pending=%d syncing=%d tick=%u lock=%d",
                ps->peer_id, ctx->lock_join_pending ? 1 : 0, mod_lockstep_syncing() ? 1 : 0,
                mod_lockstep_sim_tick(), mod_lockstep_active() ? 1 : 0);
    mod_net_send_session_peer(net, peer, ctx);
  }
  if (ps->pending_lock_phys) {
    NG_LOG_INFO("lockstep: send PHYS peer=%u bytes=%d sent=%d tick=%u", ps->peer_id,
                ps->lock_phys_size, ps->lock_phys_sent, ps->lock_phys_tick);
    mod_net_send_lock_phys_chunks(net, peer, ctx, ps);
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
static bool g_net_gateway = false;
static char g_upstream_host[64];
static uint16_t g_upstream_port = 0;

void mod_net_set_gateway(bool gateway) { g_net_gateway = gateway; }

bool mod_net_is_gateway(void) { return g_net_ctx.gateway || g_net_gateway; }

void mod_net_configure_upstream(const char *host, uint16_t port) {
  if (host) {
    strncpy(g_upstream_host, host, sizeof(g_upstream_host) - 1);
    g_upstream_host[sizeof(g_upstream_host) - 1] = '\0';
  }
  g_upstream_port = port;
}

// agent: composer-2.5 | 2026-07-29 | upstream endpoint accessor | 8b77aa
void mod_net_upstream_endpoint(char *host, size_t host_cap, uint16_t *port) {
  if (host && host_cap > 0) {
    strncpy(host, g_upstream_host, host_cap - 1);
    host[host_cap - 1] = '\0';
  }
  if (port) {
    *port = g_upstream_port;
  }
}

bool mod_net_upstream_connected(void) {
  return g_net_ctx.net_upstream != NULL && ng_net_connected(g_net_ctx.net_upstream);
}

bool mod_net_is_authoritative(void) { return mod_net_is_gateway() && !mod_net_upstream_connected(); }
#endif

static NgNet *mod_net_client_link(ModNetCtx *ctx) {
#if defined(NG_HAS_EMBEDDED)
  if (ctx->gateway) {
    return ctx->net_client ? ctx->net_client : ctx->net;
  }
#endif
  return ctx->net;
}

// agent: composer-2.5 | 2026-07-30 | send READY via upstream link | 183632
/* Gateway with root: lock replies must reach ngame_server, not loopback. */
static NgNet *mod_net_auth_send_link(ModNetCtx *ctx) {
#if defined(NG_HAS_EMBEDDED)
  if (ctx->gateway && ctx->net_upstream && ng_net_connected(ctx->net_upstream)) {
    return ctx->net_upstream;
  }
#endif
  return mod_net_client_link(ctx);
}

bool mod_net_is_connected(void) {
#if defined(NG_HAS_EMBEDDED)
  if (mod_net_is_gateway()) {
    return g_net_ctx.loopback != NULL && g_net_ctx.net_client != NULL &&
           ng_net_connected(g_net_ctx.net_client);
  }
#endif
  NgNet *link = mod_net_client_link(&g_net_ctx);
  return link != NULL && ng_net_connected(link);
}

#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
bool mod_net_has_clients(void) {
#if defined(NG_HAS_EMBEDDED)
  if (g_net_ctx.gateway && g_net_ctx.net && ng_net_connected(g_net_ctx.net)) {
    return true;
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

#if defined(NG_HAS_EMBEDDED)
static void mod_net_forward_upstream(ModNetCtx *ctx, const uint8_t *data, size_t len,
                                     uint8_t channel, bool reliable) {
  if (!ctx || !ctx->net_upstream || !ng_net_connected(ctx->net_upstream) || !data || len == 0) {
    return;
  }
  ng_net_send(ctx->net_upstream, data, len, channel, reliable);
  ng_net_flush(ctx->net_upstream);
}

static bool mod_net_begin_upstream_cmd(ModNetCtx *ctx, const char *line, bool for_console) {
  if (!ctx || !line || !ctx->net_upstream || !ng_net_connected(ctx->net_upstream)) {
    return false;
  }
  if (ctx->cmd_inflight) {
    return false;
  }
  if (!ng_proto_encode_cmd(&ctx->tx_buf, ++ctx->seq, line)) {
    return false;
  }
  ctx->cmd_inflight = true;
  ctx->upstream_pending_reply = true;
  ctx->console_cmd_pending = for_console;
  ctx->upstream_reply[0] = '\0';
  ng_net_send(ctx->net_upstream, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_RELIABLE, true);
  ng_net_flush(ctx->net_upstream);
  return true;
}

static void mod_net_finish_upstream_cmd(ModNetCtx *ctx, const char *text) {
  if (!ctx || !ctx->upstream_pending_reply) {
    return;
  }
  if (text && text[0] != '\0') {
    strncpy(ctx->upstream_reply, text, sizeof(ctx->upstream_reply) - 1);
    ctx->upstream_reply[sizeof(ctx->upstream_reply) - 1] = '\0';
  }
  ctx->cmd_inflight = false;
  ctx->upstream_pending_reply = false;
  if (ctx->console_cmd_pending) {
    NgMsg reply = {
        .kind = NG_MSG_REPLY,
        .from = NG_BUS_NET,
        .to = NG_BUS_CONSOLE,
        .text = ctx->upstream_reply[0] != '\0' ? ctx->upstream_reply : "upstream ok",
    };
    ng_bus_publish(&reply);
    ctx->console_cmd_pending = false;
  }
}

static bool mod_net_upstream_reply_from_packet(NgProtoBuf *buf, NgProtoHeader *h, char *text,
                                               size_t text_cap) {
  if (!buf || !h || !text || text_cap == 0) {
    return false;
  }
  buf->pos = sizeof(NgProtoHeader);
  if (h->type == NG_PKT_CMD_REPLY) {
    return ng_proto_decode_text(buf, text, text_cap);
  }
  if (h->type == NG_PKT_ACTION_RESULT) {
    NgActionResult result = {0};
    if (!ng_proto_decode_action_result(buf, &result)) {
      return false;
    }
    if (result.reply[0] != '\0') {
      strncpy(text, result.reply, text_cap - 1);
      text[text_cap - 1] = '\0';
      return true;
    }
  }
  return false;
}

static void mod_net_handle_upstream_packet(NgNet *net, NgNetPeer *peer, const uint8_t *data,
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
  case NG_PKT_REGISTER_ACK: {
    NgRegisterAck ack = {0};
    if (!ng_proto_decode_register_ack(buf, &ack)) {
      return;
    }
    ctx->assigned_agent_port = ack.agent_port;
    ctx->register_pending = false;
    // agent: composer-2.5 | 2026-07-30 | async upstream connect status | 421223
    NG_LOG_INFO("Registered with the game server (peer %u). Waiting for the world...",
                ack.peer_id);
    if (ctx->upstream_phase < 3) {
      ctx->upstream_phase = 3;
      ctx->upstream_phase_t0 = GetTime();
    }
    break;
  }
  case NG_PKT_SESSION:
  case NG_PKT_SNAPSHOT:
  case NG_PKT_STATE_UPDATE:
  case NG_PKT_STATE_BATCH:
  case NG_PKT_ACTION_RESULT:
  case NG_PKT_CMD_REPLY:
  // agent: composer-2.5 | 2026-07-30 | forward LOCK packets upstream | 5e79c9
  case NG_PKT_LOCK_INPUT:
  case NG_PKT_LOCK_ACK:
  case NG_PKT_LOCK_HASH:
  case NG_PKT_LOCK_PAUSE:
  case NG_PKT_LOCK_PHYS:
  case NG_PKT_LOCK_READY:
  case NG_PKT_LOCK_RESUME:
    if (h.type == NG_PKT_SNAPSHOT) {
      NgSnapshot snap = {0};
      bool delta = false;
      NgProtoBuf snap_buf = *buf;
      if (ng_proto_decode_snapshot(&snap_buf, &snap, &delta)) {
        mod_net_update_root_mirror(&snap);
      }
    }
    mod_net_handle_client_packet(ctx->net_client, NULL, data, len, channel, ctx);
    if (h.type == NG_PKT_CMD_REPLY || h.type == NG_PKT_ACTION_RESULT) {
      char reply_text[1024];
      reply_text[0] = '\0';
      mod_net_upstream_reply_from_packet(buf, &h, reply_text, sizeof(reply_text));
      mod_net_finish_upstream_cmd(ctx, reply_text);
    }
    break;
  default:
    break;
  }
}

bool mod_net_gateway_upstream_cmd(const char *line, char *reply, size_t reply_cap) {
  ModNetCtx *ctx = &g_net_ctx;
  if (!mod_net_begin_upstream_cmd(ctx, line, false)) {
    return false;
  }
  const double deadline = GetTime() + 2.0;
  while (ctx->cmd_inflight && GetTime() < deadline) {
    ng_net_poll_wait(ctx->net_upstream, mod_net_handle_upstream_packet, ctx, 1);
    mod_net_client_recv(ctx);
  }
  if (ctx->cmd_inflight) {
    mod_net_finish_upstream_cmd(ctx, "upstream cmd timeout");
  }
  for (int i = 0; i < 32; i++) {
    ng_net_poll_wait(ctx->net_upstream, mod_net_handle_upstream_packet, ctx, 1);
    mod_net_client_recv(ctx);
  }
  if (reply && reply_cap > 0) {
    if (ctx->upstream_reply[0] != '\0') {
      strncpy(reply, ctx->upstream_reply, reply_cap - 1);
      reply[reply_cap - 1] = '\0';
    } else {
      reply[0] = '\0';
    }
  }
  return true;
}

void mod_net_gateway_status_text(char *out, size_t cap) {
  // agent: composer-2.5 | 2026-07-30 | human upstream status text | 84c654
  if (!out || cap == 0) {
    return;
  }
  ModNetCtx *ctx = &g_net_ctx;
  if (ctx->upstream_host[0] == '\0' || ctx->upstream_port == 0) {
    snprintf(out, cap, "Playing offline (no remote server).");
    return;
  }
  switch (ctx->upstream_phase) {
  case 1:
    snprintf(out, cap, "Connecting to %s:%u...", ctx->upstream_host, ctx->upstream_port);
    break;
  case 2:
    snprintf(out, cap, "Connected to %s:%u — registering...", ctx->upstream_host,
             ctx->upstream_port);
    break;
  case 3:
    snprintf(out, cap, "Connected to %s:%u — loading world...", ctx->upstream_host,
             ctx->upstream_port);
    break;
  case 4:
    snprintf(out, cap, "Connected to %s:%u.", ctx->upstream_host, ctx->upstream_port);
    break;
  case 5:
    snprintf(out, cap, "Not connected to %s:%u.", ctx->upstream_host, ctx->upstream_port);
    break;
  default:
    snprintf(out, cap, "Looking up game server %s:%u...", ctx->upstream_host,
             ctx->upstream_port);
    break;
  }
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

  mod_scene_runtime_use_server();

  NgProtoBuf *buf = &ctx->rx_buf;
  mod_net_rx_load(ctx, data, len);
  buf->pos = sizeof(NgProtoHeader);

  NgProtoHeader h;
  buf->pos = 0;
  if (!ng_proto_read_header(buf, &h) || h.magic != NG_PROTO_MAGIC) {
    return;
  }

  switch (h.type) {
  case NG_PKT_REGISTER: {
#if defined(NG_SERVER)
    NgRegisterReq req = {0};
    if (!ng_proto_decode_register(buf, &req)) {
      return;
    }
    NetPeerState *ps = (NetPeerState *)ng_net_peer_data(peer);
    if (!ps) {
      return;
    }
    ps->role = NG_PEER_DEPENDENT;
    ps->registered = true;
    strncpy(ps->name, req.name, sizeof(ps->name) - 1);
    ps->name[sizeof(ps->name) - 1] = '\0';
    ps->assigned_agent_port = mod_net_alloc_agent_port();
    NgRegisterAck ack = {
        .peer_id = ps->peer_id,
        .role = NG_PEER_DEPENDENT,
        .agent_port = ps->assigned_agent_port,
        .root_game_port = ctx->port,
    };
    if (!ng_proto_encode_register_ack(&ctx->tx_buf, ++ctx->seq, &ack)) {
      return;
    }
    ng_net_send_to(net, peer, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_RELIABLE, true);
    NG_LOG_INFO("dependent registered name=%s agent=%u", ps->name, ps->assigned_agent_port);
#endif
    break;
  }
  case NG_PKT_INPUT: {
    (void)ctx;
    break;
  }
  case NG_PKT_CMD: {
    char line[256];
    if (!ng_proto_decode_cmd(buf, line, sizeof(line))) {
      return;
    }
#if defined(NG_HAS_EMBEDDED)
    if (ctx->gateway && ctx->net_upstream && ng_net_connected(ctx->net_upstream)) {
      mod_net_forward_upstream(ctx, data, len, channel, true);
      return;
    }
#endif
    mod_net_exec_host_cmd(ctx, net, peer, line, h.seq);
    break;
  }
  case NG_PKT_STATE_UPDATE: {
    NgStateUpdate update = {.tick = h.tick};
    if (!ng_proto_decode_state_update(buf, &update)) {
      return;
    }
    // agent: composer-2.5 | 2026-07-30 | STATE sync uses server graph | ef6ea6
    /* Tick/fixed_step leave the view runtime active; shared registry lives on server. */
    mod_scene_runtime_use_server();
    const NgSyncMode sync = mod_scene_graph_sync_for_entity(update.entity_id);
    if (sync == NG_SYNC_OWNER) {
      NetPeerState *ps = (NetPeerState *)ng_net_peer_data(peer);
      if (!ps || ps->peer_id != ctx->controller_id) {
        return;
      }
    } else if (sync != NG_SYNC_SHARED) {
      return;
    }
    // agent: composer-2.5 | 2026-07-29 | host assigns shared state seq | 5f8b3d
    update.seq = ++ctx->seq;
    mod_scene_apply_remote(&update);
    if (!ng_proto_encode_state_update(&ctx->tx_buf, update.seq, &update)) {
      return;
    }
    NetRelayCtx relay = {.net_ctx = ctx, .from = peer};
    ng_net_foreach_peer(net, mod_net_relay_state_update, &relay);
#if defined(NG_HAS_EMBEDDED)
    mod_net_forward_upstream(ctx, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_UNRELIABLE, false);
#endif
    break;
  }
  case NG_PKT_STATE_BATCH: {
    NgStateUpdate updates[16];
    int count = 0;
    if (!ng_proto_decode_state_batch(buf, updates, 16, &count)) {
      return;
    }
    // agent: composer-2.5 | 2026-07-30 | STATE sync uses server graph | ef6ea6
    mod_scene_runtime_use_server();
    int kept = 0;
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
      updates[i].seq = ++ctx->seq;
      mod_scene_apply_remote(&updates[i]);
      updates[kept++] = updates[i];
    }
    if (kept == 0) {
      return;
    }
    if (!ng_proto_encode_state_batch(&ctx->tx_buf, ++ctx->seq, h.tick, updates, kept)) {
      return;
    }
    NetRelayCtx relay = {.net_ctx = ctx, .from = peer};
    ng_net_foreach_peer(net, mod_net_relay_state_update, &relay);
#if defined(NG_HAS_EMBEDDED)
    mod_net_forward_upstream(ctx, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_UNRELIABLE, false);
#endif
    break;
  }
  case NG_PKT_STATE_ACK: {
    // agent: composer-2.5 | 2026-07-28 | host accepts state ack pkt | 2c89bf
    // agent: composer-2.5 | 2026-07-30 | priority flush ack track | eda41b
    uint32_t entity_id = 0;
    uint16_t ack_seq = 0;
    if (ng_proto_decode_state_ack(buf, &entity_id, &ack_seq)) {
      mod_scene_runtime_use_server();
      mod_scene_graph_note_ack(entity_id, ack_seq);
    }
    break;
  }
  // agent: composer-2.5 | 2026-07-29 | lockstep net relay gate | dc281e
  case NG_PKT_LOCK_INPUT: {
    NgLockInputPkt pkt = {0};
    if (!ng_proto_decode_lock_input(buf, &pkt)) {
      return;
    }
    for (uint8_t i = 0; i < pkt.count; i++) {
      mod_lockstep_store_remote_input(pkt.peer_id, pkt.base_tick + i, pkt.bits[i]);
    }
    if (!ng_proto_encode_lock_input(&ctx->tx_buf, ++ctx->seq, &pkt)) {
      return;
    }
    NetRelayCtx relay = {.net_ctx = ctx, .from = peer};
    ng_net_foreach_peer(net, mod_net_relay_state_update, &relay);
#if defined(NG_HAS_EMBEDDED)
    mod_net_forward_upstream(ctx, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_UNRELIABLE, false);
#endif
    break;
  }
  case NG_PKT_LOCK_ACK: {
    NgLockAckPkt pkt = {0};
    if (!ng_proto_decode_lock_ack(buf, &pkt)) {
      return;
    }
    mod_lockstep_store_ack(pkt.peer_id, pkt.ack_tick);
    if (!ng_proto_encode_lock_ack(&ctx->tx_buf, ++ctx->seq, &pkt)) {
      return;
    }
    NetRelayCtx relay = {.net_ctx = ctx, .from = peer};
    ng_net_foreach_peer(net, mod_net_relay_state_update, &relay);
    break;
  }
  case NG_PKT_LOCK_HASH: {
    // agent: composer-2.5 | 2026-07-30 | hash mismatch stalls sync | 10e625
    NgLockHashPkt pkt = {0};
    if (!ng_proto_decode_lock_hash(buf, &pkt)) {
      return;
    }
    if (pkt.peer_id != 0 && pkt.peer_id != (uint8_t)mod_lockstep_local_peer_id() &&
        pkt.tick != 0 && pkt.tick == mod_lockstep_last_hash_tick() &&
        pkt.hash != mod_lockstep_last_hash()) {
      // agent: composer-2.5 | 2026-07-30 | broadcast synth lock inputs | 96e06d
      /* Log only — permanent note_desync freezes the clock owner and all mirrors. */
      NG_LOG_WARN("lockstep: hash mismatch peer=%u tick=%u got=0x%08x want=0x%08x", pkt.peer_id,
                  pkt.tick, pkt.hash, mod_lockstep_last_hash());
    }
    if (!ng_proto_encode_lock_hash(&ctx->tx_buf, ++ctx->seq, &pkt)) {
      return;
    }
    NetRelayCtx relay = {.net_ctx = ctx, .from = peer};
    ng_net_foreach_peer(net, mod_net_relay_state_update, &relay);
    break;
  }
  case NG_PKT_LOCK_READY: {
    NgLockReadyPkt pkt = {0};
    if (!ng_proto_decode_lock_ready(buf, &pkt)) {
      return;
    }
    NG_LOG_INFO("lockstep: READY peer=%u tick=%u hash=0x%08x pending=%d expect=%u", pkt.peer_id,
                pkt.sim_tick, pkt.hash, ctx->lock_join_pending ? 1 : 0, ctx->lock_joining_peer);
    if (!ctx->lock_join_pending || pkt.peer_id != ctx->lock_joining_peer) {
      NG_LOG_WARN("lockstep: READY ignored");
      break;
    }
    if (pkt.hash != ctx->lock_join_hash) {
      // agent: composer-2.5 | 2026-07-30 | join ready mismatch aborts | 32918c
      // agent: composer-2.5 | 2026-07-31 | join abort end sync recover | 705ccc
      NG_LOG_ERROR("lockstep: join hash mismatch peer=%u got=0x%08x want=0x%08x — abort join",
                   pkt.peer_id, pkt.hash, ctx->lock_join_hash);
      ctx->lock_join_pending = false;
      ctx->lock_joining_peer = 0;
      /* Do not leave the session permanently paused (syncing STALL forever). */
      mod_lockstep_end_sync();
      mod_net_lockstep_broadcast_roster(ctx, net);
      break;
    }
    mod_lockstep_add_peer(pkt.peer_id);
    /* Pin clock to join snap before RESUME so host cannot step past joiner. */
    mod_lockstep_set_sim_tick(pkt.sim_tick);
    NG_LOG_INFO("lockstep: RESUME tick=%u peers local+%u", pkt.sim_tick, pkt.peer_id);
    NgLockResumePkt resume = {0};
    resume.sim_tick = pkt.sim_tick;
    resume.peer_count = 0;
    uint32_t local = mod_lockstep_local_peer_id();
    if (local) {
      resume.peer_ids[resume.peer_count++] = (uint8_t)local;
    }
    /* Include all connected net peers so mirrors share one roster. */
    ng_net_foreach_peer(net, mod_net_roster_acc_cb, &resume);
    if (pkt.peer_id != 0) {
      bool have = false;
      for (uint8_t i = 0; i < resume.peer_count; i++) {
        if (resume.peer_ids[i] == pkt.peer_id) {
          have = true;
          break;
        }
      }
      if (!have && resume.peer_count < NG_LOCK_PEER_MAX) {
        resume.peer_ids[resume.peer_count++] = pkt.peer_id;
      }
    }
    if (!ng_proto_encode_lock_resume(&ctx->tx_buf, ++ctx->seq, &resume)) {
      break;
    }
    ng_net_broadcast(net, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_RELIABLE, true);
    ng_net_flush(net);
    mod_lockstep_end_sync();
    ctx->lock_join_pending = false;
    ctx->lock_joining_peer = 0;
    break;
  }
  case NG_PKT_LOCK_PAUSE:
  case NG_PKT_LOCK_PHYS:
  case NG_PKT_LOCK_RESUME:
    /* Host originates these; ignore inbound copies. */
    break;
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

  mod_scene_runtime_use_view();

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
    mod_net_sync_view_scene(&ctx->snapshot_buf);
#endif
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
    if (!ctx->upstream_pending_reply) {
      ctx->cmd_inflight = false;
    }
    break;
  }
  case NG_PKT_SESSION: {
    NgSessionState session = {.tick = h.tick};
    if (!ng_proto_decode_session(buf, &session)) {
      return;
    }
    session.tick = h.tick;
    ctx->lock_peer_id = session.your_id;
    NG_LOG_INFO("net: SESSION scene=%s your=%u lock=%u syncing=%u snap=%u", session.scene_id,
                session.your_id, session.lockstep, session.syncing, session.snap_tick);
    mod_net_update_root_mirror_session(&session);
    // agent: composer-2.5 | 2026-07-30 | lockstep session server load | f8de1b
    // agent: composer-2.5 | 2026-07-30 | leave lockstep on scene switch | 4b3adf
    /* Lockstep peers load Box3D on the server slot. Leaving lockstep must tear that
     * world down and clear the fixed gate — otherwise cube STALLs forever. */
    if (session.lockstep) {
      /* Force reload only when scene_gen changes (real scene load). Controller
       * SESSION has snap_tick=0 — must not tear down a live lockstep world. */
      // agent: cursor-grok-4.5 | 2026-07-31 | scene gen only on load | cebf8b
      if (session.syncing) {
        mod_scene_on_session(&session);
      } else if (session.snap_tick != 0u && session.snap_tick != ctx->applied_scene_gen) {
        ctx->applied_scene_gen = session.snap_tick;
        mod_scene_on_session_forced(&session);
      } else {
        mod_scene_on_session(&session);
      }
    } else {
      mod_scene_clear_lockstep_server();
    }
    mod_scene_view_on_session(&session);
    break;
  }
  case NG_PKT_STATE_UPDATE: {
    NgStateUpdate update = {.tick = h.tick};
    if (!ng_proto_decode_state_update(buf, &update)) {
      return;
    }
    mod_scene_view_apply_remote(&update);
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
      mod_scene_view_apply_remote(&updates[i]);
      mod_net_send_state_ack(ctx, &updates[i]);
    }
    break;
  }
  // agent: composer-2.5 | 2026-07-29 | lockstep net relay gate | dc281e
  case NG_PKT_LOCK_INPUT: {
    NgLockInputPkt pkt = {0};
    if (!ng_proto_decode_lock_input(buf, &pkt)) {
      return;
    }
    for (uint8_t i = 0; i < pkt.count; i++) {
      mod_lockstep_store_remote_input(pkt.peer_id, pkt.base_tick + i, pkt.bits[i]);
    }
    break;
  }
  case NG_PKT_LOCK_ACK: {
    NgLockAckPkt pkt = {0};
    if (!ng_proto_decode_lock_ack(buf, &pkt)) {
      return;
    }
    mod_lockstep_store_ack(pkt.peer_id, pkt.ack_tick);
    break;
  }
  case NG_PKT_LOCK_HASH: {
    // agent: composer-2.5 | 2026-07-30 | hash mismatch stalls sync | 10e625
    NgLockHashPkt pkt = {0};
    if (!ng_proto_decode_lock_hash(buf, &pkt)) {
      return;
    }
    if (pkt.peer_id != 0 && pkt.peer_id != (uint8_t)mod_lockstep_local_peer_id() &&
        pkt.tick != 0 && pkt.tick == mod_lockstep_last_hash_tick() &&
        pkt.hash != mod_lockstep_last_hash()) {
      // agent: composer-2.5 | 2026-07-30 | broadcast synth lock inputs | 96e06d
      /* Log only — permanent note_desync freezes the clock owner and all mirrors. */
      NG_LOG_WARN("lockstep: hash mismatch peer=%u tick=%u got=0x%08x want=0x%08x", pkt.peer_id,
                  pkt.tick, pkt.hash, mod_lockstep_last_hash());
    }
    break;
  }
  case NG_PKT_LOCK_PAUSE: {
    NgLockPausePkt pkt = {0};
    if (!ng_proto_decode_lock_pause(buf, &pkt)) {
      return;
    }
    NG_LOG_INFO("lockstep: PAUSE tick=%u", pkt.sim_tick);
    mod_lockstep_begin_sync(pkt.sim_tick);
    break;
  }
  case NG_PKT_LOCK_PHYS: {
    // agent: cursor-grok-4.5 | 2026-07-31 | phys joiner only no fanout | c01e05
    NgLockPhysPkt pkt = {0};
    if (!ng_proto_decode_lock_phys(buf, &pkt)) {
      return;
    }
    if (pkt.total == 0 || pkt.len == 0 || pkt.offset + pkt.len > pkt.total) {
      break;
    }
    if (!ctx->lock_phys_rx || ctx->lock_phys_rx_tick != pkt.sim_tick ||
        ctx->lock_phys_rx_size != (int)pkt.total) {
      mod_net_lock_free_rx(ctx);
      ctx->lock_phys_rx = (uint8_t *)malloc(pkt.total);
      if (!ctx->lock_phys_rx) {
        break;
      }
      ctx->lock_phys_rx_size = (int)pkt.total;
      ctx->lock_phys_rx_got = 0;
      ctx->lock_phys_rx_tick = pkt.sim_tick;
    }
    memcpy(ctx->lock_phys_rx + pkt.offset, pkt.data, pkt.len);
    if ((int)(pkt.offset + pkt.len) > ctx->lock_phys_rx_got) {
      ctx->lock_phys_rx_got = (int)(pkt.offset + pkt.len);
    }
    if (ctx->lock_phys_rx_got < ctx->lock_phys_rx_size) {
      break;
    }
    NG_LOG_INFO("lockstep: PHYS complete bytes=%d tick=%u — importing", ctx->lock_phys_rx_size,
                pkt.sim_tick);
    // agent: composer-2.5 | 2026-07-30 | lockstep session server load | f8de1b
    mod_scene_runtime_use_server();
    if (!mod_scene_physics_import(ctx->lock_phys_rx, ctx->lock_phys_rx_size)) {
      NG_LOG_ERROR("lockstep: phys import failed");
      mod_net_lock_free_rx(ctx);
      break;
    }
    mod_lockstep_set_sim_tick(pkt.sim_tick);
    /* Keep await_phys until RESUME so joiner cannot step on in-flight tips. */
    {
      NgLockReadyPkt ready = {
          .peer_id = ctx->lock_peer_id ? ctx->lock_peer_id : (uint8_t)mod_lockstep_local_peer_id(),
          .sim_tick = pkt.sim_tick,
          .hash = mod_scene_physics_checksum(),
      };
      /* Host accepts READY only from lock_joining_peer; mirror READY is ignored. */
      NG_LOG_INFO("lockstep: send READY peer=%u tick=%u hash=0x%08x", ready.peer_id, ready.sim_tick,
                  ready.hash);
      if (ng_proto_encode_lock_ready(&ctx->tx_buf, ++ctx->seq, &ready)) {
        NgNet *link = mod_net_auth_send_link(ctx);
        if (link && ng_net_connected(link)) {
          ng_net_send(link, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_RELIABLE, true);
          ng_net_flush(link);
        }
      }
    }
    mod_net_lock_free_rx(ctx);
    break;
  }
  case NG_PKT_LOCK_RESUME: {
    NgLockResumePkt pkt = {0};
    if (!ng_proto_decode_lock_resume(buf, &pkt)) {
      return;
    }
    NG_LOG_INFO("lockstep: RESUME tick=%u peer_count=%u", pkt.sim_tick, pkt.peer_count);
    /* Authoritative roster — drop disconnect ghosts or all_have STALLs forever. */
    // agent: cursor-grok-4.5 | 2026-07-31 | apply authoritative resume roster | 3cc0b8
    mod_lockstep_apply_roster(pkt.peer_ids, (int)pkt.peer_count);
    /* Join/PAUSE ends on RESUME. Host skips roster pulses during join_pending,
     * so await_phys here means PHYS imported and READY was sent. */
    // agent: cursor-grok-4.5 | 2026-07-31 | phys joiner only no fanout | c01e05
    if (mod_lockstep_syncing() || mod_lockstep_awaiting_phys()) {
      mod_lockstep_set_sim_tick(pkt.sim_tick);
      mod_lockstep_end_sync();
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
    // agent: composer-2.5 | 2026-07-29 | lockstep net relay gate | dc281e
    NetPeerState *ps = (NetPeerState *)calloc(1, sizeof(NetPeerState));
    ps->peer_id = ++ctx->next_peer_id;
    ps->role = NG_PEER_THIN;
    if (ctx->controller_id == 0) {
      ctx->controller_id = ps->peer_id;
    }
    ng_net_peer_set_data(peer, ps);
    {
      uint32_t send = 0, local = 0, sim = 0;
      int peers = 0, started = 0, syncing = 0, awaitp = 0;
      mod_lockstep_debug_full(&send, &peers, &started, &local, &syncing, &awaitp, &sim);
      NG_LOG_INFO("net: peer connect id=%u lock active=%d sim=%u send=%u peers=%d started=%d "
                  "syncing=%d await=%d phys_lock=%d needs_sync=%d",
                  ps->peer_id, mod_lockstep_active() ? 1 : 0, sim, send, peers, started, syncing,
                  awaitp, mod_scene_physics_is_lockstep() ? 1 : 0,
                  mod_lockstep_needs_join_sync() ? 1 : 0);
    }
    if (mod_lockstep_needs_join_sync()) {
      // agent: cursor-grok-4.5 | 2026-07-31 | phys joiner only no fanout | c01e05
      /* Joiner-only Box3D dump. Existing peers already share the world at T —
       * re-importing the save into them desyncs hashes (not Gaffer late-join). */
      mod_scene_runtime_use_server();
      const uint32_t tick = mod_lockstep_sim_tick();
      mod_lockstep_begin_sync(tick);
      ctx->lock_join_pending = true;
      ctx->lock_joining_peer = ps->peer_id;
      ctx->lock_join_hash = mod_scene_physics_checksum();
      NG_LOG_INFO("lockstep: LATE JOIN peer=%u pause tick=%u hash=0x%08x", ps->peer_id, tick,
                  ctx->lock_join_hash);
      mod_net_broadcast_lock_pause(ctx, net, tick);
      uint8_t *phys = NULL;
      int phys_size = 0;
      if (mod_scene_physics_export(&phys, &phys_size) && phys && phys_size > 0) {
        ps->lock_phys_data = phys;
        ps->lock_phys_size = phys_size;
        ps->lock_phys_sent = 0;
        ps->lock_phys_tick = tick;
        ps->pending_lock_phys = true;
        ps->pending_connect_snap = false;
        NG_LOG_INFO("lockstep: phys export ok peer=%u bytes=%d (joiner only)", ps->peer_id,
                    phys_size);
      } else {
        NG_LOG_ERROR("lockstep: phys export failed peer=%u", ps->peer_id);
        if (phys) {
          b3FreeSaveData(phys, phys_size);
        }
        ps->pending_connect_snap = true;
      }
      ps->pending_connect_session = true;
      // agent: composer-2.5 | 2026-07-30 | flush late-join connect immediately | fc4cb2
      /* Don't wait for the next poll — joiner is blocked in sync_view. */
      mod_net_send_connect_snapshot(net, peer, ctx);
      ng_net_flush(net);
    } else {
      // agent: composer-2.5 | 2026-07-30 | solo lockstep one peer | a8feaa
      // agent: cursor-grok-4.5 | 2026-07-31 | net connect log rename | af466e
      NG_LOG_INFO("net: cold connect peer=%u (mid_sim=%d lock=%d)", ps->peer_id,
                  mod_lockstep_needs_join_sync() ? 1 : 0, mod_lockstep_active() ? 1 : 0);
      mod_net_fill_snapshot_buf(ctx);
      ps->pending_connect_snap = true;
      ps->pending_connect_session = true;
    }
  } else {
    NetPeerState *ps = (NetPeerState *)ng_net_peer_data(peer);
    const bool was_controller = ps && ps->peer_id == ctx->controller_id;
    NG_LOG_INFO("net: peer disconnect id=%u controller=%d", ps ? ps->peer_id : 0,
                was_controller ? 1 : 0);
    if (ps) {
      // agent: composer-2.5 | 2026-07-30 | remove peer on disconnect | cbe0b7
      mod_lockstep_remove_peer(ps->peer_id);
      if (ctx->lock_join_pending && ctx->lock_joining_peer == ps->peer_id) {
        NG_LOG_WARN("lockstep: joiner disconnect peer=%u — end sync", ps->peer_id);
        ctx->lock_join_pending = false;
        ctx->lock_joining_peer = 0;
        mod_lockstep_end_sync();
      }
      mod_net_lock_free_peer_phys(ps);
    }
    free(ps);
    ng_net_peer_set_data(peer, NULL);
    /* Count still-connected ENet peers (this peer already detached). */
    // agent: cursor-grok-4.5 | 2026-07-31 | prune peers when lobby empty | 5390c4
    int live = 0;
    ng_net_foreach_peer(net, mod_net_count_peers_cb, &live);
    if (mod_lockstep_active() && live == 0) {
      /* Hard-kill can leave lockstep ghosts until timeout — wipe lobby now. */
      mod_lockstep_clear_peers();
      ctx->lock_join_pending = false;
      ctx->lock_joining_peer = 0;
      if (mod_lockstep_syncing() || mod_lockstep_awaiting_phys()) {
        mod_lockstep_end_sync();
      }
      NG_LOG_INFO("lockstep: empty lobby — cleared peers/join");
    }
    if (was_controller) {
      ctx->controller_id = 0;
      ng_net_foreach_peer(net, mod_net_pick_controller, ctx);
      mod_net_broadcast_session(ctx, net);
    }
    /* Tell survivors the peer is gone — one reliable roster, not a spam loop. */
    // agent: cursor-grok-4.5 | 2026-07-31 | roster remove on disconnect | c9f9cd
    if (mod_lockstep_active() && mod_lockstep_is_clock_owner() && live > 0) {
      mod_net_lockstep_broadcast_roster(ctx, net);
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
#if defined(NG_HAS_EMBEDDED)
    if (ctx->gateway && ctx->net_upstream && ng_net_connected(ctx->net_upstream)) {
      if (ctx->cmd_inflight) {
        NgMsg busy = {
            .kind = NG_MSG_REPLY,
            .from = NG_BUS_NET,
            .to = NG_BUS_CONSOLE,
            .text = "cmd busy (wait for prior reply)",
        };
        ng_bus_publish(&busy);
        return true;
      }
      if (mod_net_begin_upstream_cmd(ctx, msg->line, true)) {
        return true;
      }
      NgMsg fail = {
          .kind = NG_MSG_REPLY,
          .from = NG_BUS_NET,
          .to = NG_BUS_CONSOLE,
          .text = "upstream cmd failed",
      };
      ng_bus_publish(&fail);
      return true;
    }
    if (ctx->gateway && ctx->net) {
      ctx->cmd_inflight = true;
      mod_net_exec_host_cmd(ctx, ctx->net, NULL, msg->line, ++ctx->seq);
      ng_net_poll(ctx->net, mod_net_handle_host_packet, ctx);
      mod_net_client_recv(ctx);
      if (ctx->pending_reply) {
        NgMsg reply = {
            .kind = NG_MSG_REPLY,
            .from = NG_BUS_NET,
            .to = NG_BUS_CONSOLE,
            .text = ctx->pending_reply_text,
        };
        ng_bus_publish(&reply);
        ctx->pending_reply = false;
      }
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
    return true;
  }
#endif
  return false;
}

#if defined(NG_HAS_EMBEDDED)
void mod_net_gateway_resync(void) {
  ModNetCtx *ctx = &g_net_ctx;
  if (!ctx->gateway || !ctx->net || mod_net_skip_local_boot()) {
    return;
  }
  mod_scene_runtime_use_server();
  if (!mod_scene_is_loaded()) {
    return;
  }
  mod_net_fill_snapshot_buf(ctx);
  mod_net_broadcast_session(ctx, ctx->net);
  ng_net_foreach_peer(ctx->net, mod_net_send_snapshot_peer, ctx);
  ng_net_flush(ctx->net);
  mod_net_client_recv(ctx);
}

// agent: composer-2.5 | 2026-07-30 | async upstream connect status | 421223
static void mod_net_upstream_send_register(ModNetCtx *ctx) {
  if (!ctx || !ctx->net_upstream || !ng_net_connected(ctx->net_upstream)) {
    return;
  }
  if (ctx->upstream_register_sent || ctx->assigned_agent_port != 0) {
    return;
  }
  NgRegisterReq req = {0};
  snprintf(req.name, sizeof(req.name), "gateway-%d", (int)getpid());
  req.proto_ver = NG_PROTO_VERSION;
  if (!ng_proto_encode_register(&ctx->tx_buf, ++ctx->seq, &req)) {
    NG_LOG_ERROR("Could not build the registration packet for the game server.");
    return;
  }
  ctx->register_pending = true;
  ctx->upstream_register_sent = true;
  ctx->upstream_phase = 2;
  ctx->upstream_phase_t0 = GetTime();
  ng_net_send(ctx->net_upstream, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_RELIABLE, true);
  ng_net_flush(ctx->net_upstream);
  NG_LOG_INFO("Link is up to %s:%u — introducing this client to the server...",
              ctx->upstream_host, ctx->upstream_port);
}

static void mod_net_upstream_log_phase(ModNetCtx *ctx, int phase) {
  if (!ctx || phase == ctx->upstream_phase_logged) {
    return;
  }
  ctx->upstream_phase_logged = phase;
  switch (phase) {
  case 1:
    NG_LOG_INFO("Connecting to the game server at %s:%u...", ctx->upstream_host,
                ctx->upstream_port);
    break;
  case 2:
    /* logged in send_register */
    break;
  case 3:
    /* detailed register log already printed */
    break;
  case 4:
    NG_LOG_INFO("Connected to %s:%u — world is ready.", ctx->upstream_host, ctx->upstream_port);
    break;
  case 5:
    NG_LOG_WARN("Not connected to the game server at %s:%u.", ctx->upstream_host,
                ctx->upstream_port);
    break;
  default:
    break;
  }
}

static void mod_net_upstream_tick(ModNetCtx *ctx) {
  if (!ctx || !ctx->gateway || ctx->upstream_host[0] == '\0' || ctx->upstream_port == 0) {
    return;
  }
  if (!ctx->net_upstream) {
    ctx->upstream_phase = 5;
    mod_net_upstream_log_phase(ctx, 5);
    return;
  }

  const bool connected = ng_net_connected(ctx->net_upstream);
  const double now = GetTime();

  if (connected) {
    if (!ctx->upstream_was_connected) {
      ctx->upstream_was_connected = true;
      NG_LOG_INFO("Connected to the game server at %s:%u.", ctx->upstream_host,
                  ctx->upstream_port);
    }
    mod_net_upstream_send_register(ctx);
    if (ctx->assigned_agent_port != 0) {
      if (mod_scene_view_is_loaded() || ctx->have_baseline) {
        ctx->upstream_phase = 4;
      } else if (ctx->upstream_phase < 3) {
        ctx->upstream_phase = 3;
        ctx->upstream_phase_t0 = now;
      }
    } else if (ctx->upstream_register_sent) {
      ctx->upstream_phase = 2;
    } else {
      ctx->upstream_phase = 2;
    }
  } else {
    if (ctx->upstream_was_connected) {
      ctx->upstream_was_connected = false;
      ctx->upstream_register_sent = false;
      ctx->register_pending = false;
      ctx->assigned_agent_port = 0;
      ctx->upstream_phase = 5;
      NG_LOG_WARN("Lost connection to the game server at %s:%u.", ctx->upstream_host,
                  ctx->upstream_port);
    } else if (ctx->upstream_phase != 5) {
      ctx->upstream_phase = 1;
      if (now - ctx->upstream_last_hint_log >= 2.0) {
        ctx->upstream_last_hint_log = now;
        const double waited = now - ctx->upstream_phase_t0;
        NG_LOG_INFO("Still connecting to %s:%u (%.0fs)... is the server running?",
                    ctx->upstream_host, ctx->upstream_port, waited);
      }
    }
  }

  mod_net_upstream_log_phase(ctx, ctx->upstream_phase);
}

void mod_net_gateway_sync_view(void) {
  // agent: composer-2.5 | 2026-07-30 | nonblock sync view drain | 3995f3
  /* Non-blocking: one poll pass. Frame loop finishes sync without freezing. */
  ModNetCtx *ctx = &g_net_ctx;
  if (!ctx->gateway || !ctx->net_upstream) {
    return;
  }
  ng_net_poll(ctx->net_upstream, mod_net_handle_upstream_packet, ctx);
  mod_net_client_recv(ctx);
  mod_net_upstream_tick(ctx);
}
#endif

static bool mod_net_init(void *vctx) {
  ModNetCtx *ctx = (ModNetCtx *)vctx;
  char host_save[64];
  const uint16_t port_save = ctx->port;
#if defined(NG_HAS_EMBEDDED)
  const bool gateway_save = g_net_gateway;
  char upstream_host_save[64];
  const uint16_t upstream_port_save = g_upstream_port;
  strncpy(upstream_host_save, g_upstream_host, sizeof(upstream_host_save) - 1);
  upstream_host_save[sizeof(upstream_host_save) - 1] = '\0';
#else
  const bool gateway_save = false;
  char upstream_host_save[64] = {0};
  const uint16_t upstream_port_save = 0;
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
  ctx->gateway = gateway_save;
  strncpy(ctx->upstream_host, upstream_host_save, sizeof(ctx->upstream_host) - 1);
  ctx->upstream_port = upstream_port_save;
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
  if (ctx->gateway) {
    ctx->loopback = ng_net_loopback_create();
    if (!ctx->loopback) {
      return false;
    }
    ctx->net = ng_net_loopback_host(ctx->loopback);
    ctx->net_client = ng_net_loopback_client(ctx->loopback);
    ng_net_set_peer_fn(ctx->net, mod_net_on_peer, ctx);
    ng_net_loopback_connect(ctx->loopback);
    if (ctx->upstream_host[0] != '\0' && ctx->upstream_port != 0) {
      // agent: composer-2.5 | 2026-07-30 | async upstream connect status | 421223
      ctx->upstream_phase = 1;
      ctx->upstream_phase_logged = 0;
      ctx->upstream_phase_t0 = GetTime();
      ctx->upstream_last_hint_log = 0.0;
      ctx->upstream_register_sent = false;
      ctx->upstream_was_connected = false;
      NG_LOG_INFO("Connecting to the game server at %s:%u...", ctx->upstream_host,
                  ctx->upstream_port);
      ctx->upstream_phase_logged = 1;
      ctx->net_upstream =
          ng_net_create(NG_NET_ROLE_CLIENT, ctx->upstream_host, ctx->upstream_port);
      if (!ctx->net_upstream) {
        ctx->upstream_phase = 5;
        NG_LOG_WARN("Could not start a connection to %s:%u.", ctx->upstream_host,
                    ctx->upstream_port);
        ctx->upstream_phase_logged = 5;
      }
    }
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
  if (ctx->net_upstream) {
    ng_net_destroy(ctx->net_upstream);
    ctx->net_upstream = NULL;
  }
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
  if (ctx->gateway) {
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

#if defined(NG_HAS_EMBEDDED)
void mod_net_gateway_host_poll(void) {
  ModNetCtx *ctx = &g_net_ctx;
  if (!ctx->gateway || !ctx->net) {
    return;
  }
  ng_net_poll(ctx->net, mod_net_handle_host_packet, ctx);
  ng_net_foreach_peer(ctx->net, mod_net_send_connect_snapshot, ctx);
  ng_net_flush(ctx->net);
  if (ctx->net_upstream) {
    ng_net_poll(ctx->net_upstream, mod_net_handle_upstream_packet, ctx);
    ng_net_flush(ctx->net_upstream);
    mod_net_client_recv(ctx);
  }
  // agent: composer-2.5 | 2026-07-30 | async upstream connect status | 421223
  mod_net_upstream_tick(ctx);
}
#endif

#if defined(NG_HAS_EMBEDDED) || !defined(NG_SERVER)
void mod_net_poll_recv(void) {
  mod_net_client_recv(&g_net_ctx);
}
#endif

static bool mod_net_on_msg_wrap(const NgMsg *msg, void *vctx) {
  if (msg->kind == NG_MSG_TICK && msg->to == NG_BUS_ANY) {
    mod_net_tick(msg, vctx);
  }
  return mod_net_on_msg(msg, vctx);
}

// agent: composer-2.5 | 2026-07-29 | Extend NgModOps side fixed_step | 4f3d39
static const NgModOps g_net_ops = {
    .name = "net",
    .dest = NG_BUS_NET,
    .side = NG_MOD_SIDE_BOTH,
    .init = mod_net_init,
    .shutdown = mod_net_shutdown,
    .on_msg = mod_net_on_msg_wrap,
    .fixed_step = NULL,
};

#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
static void mod_net_send_state_peer(NgNet *net, NgNetPeer *peer, void *vctx) {
  ModNetCtx *ctx = (ModNetCtx *)vctx;
  ng_net_send_to(net, peer, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_UNRELIABLE, false);
}
#endif

// agent: composer-2.5 | 2026-07-29 | lockstep net relay gate | dc281e
// agent: composer-2.5 | 2026-07-30 | broadcast synth lock inputs | 96e06d
static void mod_net_send_lock_tx(ModNetCtx *ctx) {
  if (!ctx || ctx->tx_buf.len == 0) {
    return;
  }
#if defined(NG_SERVER)
  if (ctx->net) {
    ng_net_foreach_peer(ctx->net, mod_net_send_state_peer, ctx);
    ng_net_flush(ctx->net);
  }
#elif defined(NG_HAS_EMBEDDED) || !defined(NG_SERVER)
#if defined(NG_HAS_EMBEDDED)
  if (ctx->gateway && ctx->net) {
    ng_net_foreach_peer(ctx->net, mod_net_send_state_peer, ctx);
    ng_net_flush(ctx->net);
  }
  if (ctx->gateway && ctx->net_upstream && ng_net_connected(ctx->net_upstream)) {
    ng_net_send(ctx->net_upstream, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_UNRELIABLE, false);
    ng_net_flush(ctx->net_upstream);
    return;
  }
#endif
  {
    NgNet *link = mod_net_client_link(ctx);
    if (ng_net_connected(link)) {
      ng_net_send(link, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_UNRELIABLE, false);
    }
  }
#endif
}

static void mod_net_flush_lockstep(ModNetCtx *ctx) {
  if (!ctx || !mod_lockstep_active()) {
    return;
  }
#if defined(NG_HAS_EMBEDDED)
  /* Solo gateway: one process owns the clock; loopback LOCK relay floods CPU. */
  if (ctx->gateway && !(ctx->net_upstream && ng_net_connected(ctx->net_upstream))) {
    return;
  }
#endif
#if defined(NG_SERVER)
  if (!ctx->net) {
    return;
  }
#endif
  NgLockInputPkt inp = {0};
  uint32_t local_id = mod_lockstep_local_peer_id();
  if (ctx->lock_peer_id != 0) {
    local_id = ctx->lock_peer_id;
    // agent: composer-2.5 | 2026-07-30 | no clear peers on adopt id | dfad8e
    /* Adopt SESSION your_id without wiping the peer roster (that broke all_have). */
    if (mod_lockstep_local_peer_id() != local_id) {
      mod_lockstep_set_local_peer(local_id);
    }
  }
  inp.peer_id = (uint8_t)local_id;
  const int n = mod_lockstep_fill_send_window(&inp.base_tick, inp.bits, NG_LOCK_INPUT_MAX);
  if (n > 0) {
    inp.count = (uint8_t)n;
    if (ng_proto_encode_lock_input(&ctx->tx_buf, ++ctx->seq, &inp)) {
      mod_net_send_lock_tx(ctx);
    }
  }
  /* Cold roster only (tick 0). No periodic reliable RESUME — every 300 ticks
   * (~5s) starved UDP LOCK_INPUT and hard-froze peers (Gaffer anti-drift #4). */
  // agent: cursor-grok-4.5 | 2026-07-31 | drop periodic roster pulses | 9fb282
  // agent: cursor-grok-4.5 | 2026-07-31 | remove dead synth path | 7bdd5e
  if (mod_lockstep_is_clock_owner()) {
    const uint32_t st = mod_lockstep_sim_tick();
    if (st == 0u && ctx->lock_roster_sent_tick > 1u && ctx->lock_last_sim_tick > 0u) {
      ctx->lock_roster_sent_tick = 0;
      ctx->lock_roster_pulse = 0;
    }
    ctx->lock_last_sim_tick = st;
    ctx->lock_roster_pulse++;
    const bool first = (ctx->lock_roster_sent_tick == 0);
    const bool cold_due =
        (st == 0u) && (ctx->lock_roster_pulse <= 180u) && (ctx->lock_roster_pulse % 60u) == 0u &&
        (ctx->lock_roster_pulse / 60u) <= 3u;
    if (!ctx->lock_join_pending && (first || cold_due)) {
      ctx->lock_roster_sent_tick = st ? st : ctx->lock_roster_pulse;
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
      if (ctx->net) {
        mod_net_lockstep_broadcast_roster(ctx, ctx->net);
      }
#endif
    }
  }
  NgLockAckPkt ack = {
      .peer_id = (uint8_t)mod_lockstep_local_peer_id(),
      .ack_tick = mod_lockstep_highest_recv_contiguous(),
  };
  if (ng_proto_encode_lock_ack(&ctx->tx_buf, ++ctx->seq, &ack)) {
    mod_net_send_lock_tx(ctx);
  }
  const uint32_t ht = mod_lockstep_last_hash_tick();
  if (ht != 0 && ht != ctx->lock_hash_sent_tick) {
    NgLockHashPkt hp = {
        .peer_id = (uint8_t)mod_lockstep_local_peer_id(),
        .tick = ht,
        .hash = mod_lockstep_last_hash(),
    };
    if (ng_proto_encode_lock_hash(&ctx->tx_buf, ++ctx->seq, &hp)) {
      ctx->lock_hash_sent_tick = ht;
      mod_net_send_lock_tx(ctx);
    }
  }
}

static void mod_net_flush_state_update(ModNetCtx *ctx) {
  // agent: composer-2.5 | 2026-07-30 | dual channel lockstep state flush | 611cd9
  // agent: composer-2.5 | 2026-07-30 | priority flush ack track | eda41b
  /* Lockstep channel: inputs/acks/hashes. Transform channel: bodiless sync entities. */
  if (mod_lockstep_active()) {
    mod_net_flush_lockstep(ctx);
  }
  // agent: composer-2.5 | 2026-07-30 | flush when view scene loaded | 44b0e0
  /* Remotes author shared entities on the view graph; server slot may be empty. */
  if (!mod_scene_is_loaded() && !mod_scene_view_is_loaded()) {
    return;
  }
  NgStateUpdate cand[64];
  float prio[64];
  for (;;) {
    int n = 0;
    uint32_t tick = 0;
#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
    tick = mod_sim_world()->tick;
#else
    tick = ctx->last_snap_tick;
#endif
    while (n < 64 && mod_scene_take_flush(&cand[n])) {
      cand[n].tick = tick;
      NgSceneInst *inst = mod_scene_graph_inst_by_id(cand[n].entity_id);
      if (inst) {
        mod_scene_graph_prepare_wire_update(inst, &cand[n]);
      }
      prio[n] = mod_scene_graph_flush_priority(&cand[n]);
      n++;
    }
    if (n == 0) {
      return;
    }
    /* Priority sort (descending) — send hottest movers first. */
    for (int i = 0; i < n; i++) {
      for (int j = i + 1; j < n; j++) {
        if (prio[j] > prio[i]) {
          const float tp = prio[i];
          prio[i] = prio[j];
          prio[j] = tp;
          const NgStateUpdate tu = cand[i];
          cand[i] = cand[j];
          cand[j] = tu;
        }
      }
    }
    const int send_n = n < 16 ? n : 16;
    /* Re-dirty leftovers so they retry next flush. */
    for (int i = send_n; i < n; i++) {
      NgSceneInst *inst = mod_scene_graph_inst_by_id(cand[i].entity_id);
      if (inst) {
        mod_scene_graph_mark_dirty(inst, cand[i].comp_mask & ~NG_COMP_FLAGS);
      }
    }
    NgStateUpdate batch[16];
    for (int i = 0; i < send_n; i++) {
      batch[i] = cand[i];
      batch[i].seq = ++ctx->seq;
      NgSceneInst *inst = mod_scene_graph_inst_by_id(batch[i].entity_id);
      if (inst) {
        mod_scene_graph_note_sent(inst, &batch[i]);
      }
    }
    const bool ok =
        (send_n == 1)
            ? ng_proto_encode_state_update(&ctx->tx_buf, batch[0].seq, &batch[0])
            : ng_proto_encode_state_batch(&ctx->tx_buf, batch[0].seq, tick, batch, send_n);
    if (!ok) {
      return;
    }
#if defined(NG_SERVER)
    if (ctx->net) {
      ng_net_foreach_peer(ctx->net, mod_net_send_state_peer, ctx);
      ng_net_flush(ctx->net);
    }
#elif defined(NG_HAS_EMBEDDED) || !defined(NG_SERVER)
#if defined(NG_HAS_EMBEDDED)
    if (ctx->gateway && ctx->net) {
      ng_net_foreach_peer(ctx->net, mod_net_send_state_peer, ctx);
      ng_net_flush(ctx->net);
    }
    if (ctx->gateway && ctx->net_upstream && ng_net_connected(ctx->net_upstream)) {
      ng_net_send(ctx->net_upstream, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_UNRELIABLE,
                  false);
      ng_net_flush(ctx->net_upstream);
      continue;
    }
#endif
    NgNet *link = mod_net_client_link(ctx);
    if (ng_net_connected(link)) {
      ng_net_send(link, ctx->tx_buf.data, ctx->tx_buf.len, NG_CH_UNRELIABLE, false);
    }
#endif
  }
}

void mod_net_flush_scene_updates(void) {
  // agent: composer-2.5 | 2026-07-30 | flush lockstep and state updates | 84dae7
  mod_net_flush_state_update(&g_net_ctx);
}

#if defined(NG_HAS_EMBEDDED)
uint16_t mod_net_assigned_agent_port(void) { return g_net_ctx.assigned_agent_port; }

bool mod_net_skip_local_boot(void) {
  return mod_net_is_gateway() && g_net_ctx.upstream_host[0] != '\0';
}

void mod_net_root_mirror_text(char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  if (!g_root_mirror.valid) {
    snprintf(out, cap, "root n/a");
    return;
  }
  snprintf(out, cap, "root scene=%s entities=%d tick=%u", g_root_mirror.scene_id,
           g_root_mirror.entity_count, g_root_mirror.tick);
}
#endif

#if defined(NG_SERVER) || defined(NG_HAS_EMBEDDED)
void mod_net_broadcast_scene_session(void) {
  ModNetCtx *ctx = &g_net_ctx;
  /* Scene load only — bumps gen so clients force-reload (solar→solar). */
  // agent: cursor-grok-4.5 | 2026-07-31 | scene gen only on load | cebf8b
  ctx->scene_gen += 1u;
  if (ctx->scene_gen == 0u) {
    ctx->scene_gen = 1u;
  }
  ctx->session_carry_gen = true;
  if (ctx->net) {
    mod_net_broadcast_session(ctx, ctx->net);
  }
  ctx->session_carry_gen = false;
}
#endif

const NgModOps *mod_net_ops(void) { return &g_net_ops; }

void *mod_net_ctx(void) { return &g_net_ctx; }

// agent: composer-2.5 | 2026-07-29 | wire view only handlers | 3afab8
// agent: composer-2.5 | 2026-07-29 | gateway resync skip loopback | 38cd44
// agent: composer-2.5 | 2026-07-29 | async upstream console cmd | c9d0e1
// agent: composer-2.5 | 2026-07-29 | sync view from snapshot | 381657
// agent: composer-2.5 | 2026-07-29 | upstream endpoint accessor | 8b77aa
// agent: composer-2.5 | 2026-07-29 | no stale snapshot scene revert | 8aba32
// agent: composer-2.5 | 2026-07-29 | drain upstream view sync | 2705b6
// agent: composer-2.5 | 2026-07-29 | root mirror tracks sessions | 5809c3
// agent: composer-2.5 | 2026-07-29 | flush shared transforms upstream | 9d2e71
// agent: composer-2.5 | 2026-07-29 | host assigns shared state seq | 5f8b3d
// agent: composer-2.5 | 2026-07-29 | Extend NgModOps side fixed_step | 4f3d39
// agent: composer-2.5 | 2026-07-29 | gateway host state broadcast | d5b8b5
// agent: composer-2.5 | 2026-07-29 | lockstep net relay gate | dc281e
// agent: composer-2.5 | 2026-07-30 | lockstep skip transform wire | 353d7b
// agent: composer-2.5 | 2026-07-30 | solo lockstep one peer | a8feaa
// agent: composer-2.5 | 2026-07-30 | net late join sync flow | 0f03f2
// agent: composer-2.5 | 2026-07-30 | lockstep join fixes logging | 8c64cd
// agent: composer-2.5 | 2026-07-30 | forward LOCK packets upstream | 5e79c9
// agent: composer-2.5 | 2026-07-30 | send READY via upstream link | 183632
// agent: composer-2.5 | 2026-07-30 | sync_view exit on view load | 6c2018
// agent: composer-2.5 | 2026-07-30 | flush late-join connect immediately | fc4cb2
// agent: composer-2.5 | 2026-07-30 | async upstream connect status | 421223
// agent: composer-2.5 | 2026-07-30 | human upstream status text | 84c654
// agent: composer-2.5 | 2026-07-30 | nonblock sync view drain | 3995f3
// agent: composer-2.5 | 2026-07-30 | dual channel lockstep state flush | 611cd9
// agent: composer-2.5 | 2026-07-30 | flush lockstep and state updates | 84dae7
// agent: composer-2.5 | 2026-07-30 | priority flush ack track | eda41b
// agent: composer-2.5 | 2026-07-30 | hash mismatch stalls sync | 10e625
// agent: composer-2.5 | 2026-07-30 | remove peer on disconnect | cbe0b7
// agent: composer-2.5 | 2026-07-30 | join ready mismatch aborts | 32918c
// agent: composer-2.5 | 2026-07-30 | lockstep session server load | f8de1b
// agent: composer-2.5 | 2026-07-30 | broadcast synth lock inputs | 96e06d
// agent: composer-2.5 | 2026-07-30 | lockstep peer roster sync | ffd508
// agent: composer-2.5 | 2026-07-30 | leave lockstep on scene switch | 4b3adf
// agent: composer-2.5 | 2026-07-30 | STATE sync uses server graph | ef6ea6
// agent: composer-2.5 | 2026-07-30 | flush when view scene loaded | 44b0e0
// agent: composer-2.5 | 2026-07-30 | no clear peers on adopt id | dfad8e
// agent: composer-2.5 | 2026-07-30 | roster broadcast early often | 0dbf8a
// agent: composer-2.5 | 2026-07-30 | no snap roster without phys | f942ca
// agent: composer-2.5 | 2026-07-31 | join abort end sync recover | 705ccc
// agent: composer-2.5 | 2026-07-31 | rearm roster on sim restart | b74259
// agent: cursor-grok-4.5 | 2026-07-31 | throttle roster outside flush | 600f58
// agent: cursor-grok-4.5 | 2026-07-31 | phys joiner only no fanout | c01e05
// agent: cursor-grok-4.5 | 2026-07-31 | drop periodic roster pulses | 9fb282
// agent: cursor-grok-4.5 | 2026-07-31 | remove dead synth path | 7bdd5e
// agent: cursor-grok-4.5 | 2026-07-31 | roster remove on disconnect | c9f9cd
// agent: cursor-grok-4.5 | 2026-07-31 | apply authoritative resume roster | 3cc0b8
// agent: cursor-grok-4.5 | 2026-07-31 | drop scene epoch snap hack | 1bda0d
// agent: cursor-grok-4.5 | 2026-07-31 | force reload without epoch | c1e2dc
// agent: cursor-grok-4.5 | 2026-07-31 | scene gen only on load | cebf8b
// agent: cursor-grok-4.5 | 2026-07-31 | net connect log rename | af466e
// agent: cursor-grok-4.5 | 2026-07-31 | reset lockstep empty lobby | 2dafd8
// agent: cursor-grok-4.5 | 2026-07-31 | prune peers when lobby empty | 5390c4
