// agent: composer-2.5 | 2026-07-25 | ENet multi-peer transport | d7g95b
#include "ng_net.h"
#include "engine/ng_log.h"

#define ENET_IMPLEMENTATION
#include "vendor/enet.h"

#include <stdlib.h>
#include <string.h>

typedef struct NgNet NgNet;
typedef struct NgNetLoopbackPair NgNetLoopbackPair;

struct NgNetPeer {
  bool lb;
  union {
    ENetPeer *peer;
    void *lb_data;
  } u;
};

struct NgNet {
  NgNetRole role;
  ENetHost *host;
  ENetPeer *peer;
  bool connected;
  NgNetPeerFn peer_fn;
  void *peer_ctx;
#if defined(NG_HAS_EMBEDDED)
  bool lb;
  NgNetLoopbackPair *lb_pair;
  NgNetPeer *lb_peer;
#endif
};

#if defined(NG_HAS_EMBEDDED)
#define NG_LOOPBACK_QCAP 16
#define NG_LOOPBACK_PKT_MAX 8192

typedef struct NgLoopbackPkt {
  uint8_t data[NG_LOOPBACK_PKT_MAX];
  size_t len;
  uint8_t channel;
} NgLoopbackPkt;

struct NgNetLoopbackPair {
  NgLoopbackPkt to_host[NG_LOOPBACK_QCAP];
  int to_host_r;
  int to_host_w;
  NgLoopbackPkt to_client[NG_LOOPBACK_QCAP];
  int to_client_r;
  int to_client_w;
  bool linked;
  NgNet *host;
  NgNet *client;
  NgNetPeer host_peer;
  NgNetPeer client_peer;
};
#endif

// agent: composer-2.5 | 2026-07-25 | enet refcount not global kill | b8e41a
static int g_enet_users = 0;

static bool ng_net_init_once(void) {
  if (g_enet_users == 0) {
    if (enet_initialize() != 0) {
      NG_LOG_ERROR("enet_initialize failed");
      return false;
    }
  }
  g_enet_users++;
  return true;
}

static bool ng_net_peer_connected(ENetPeer *p) {
  return p && p->state == ENET_PEER_STATE_CONNECTED;
}

// agent: composer-2.5 | 2026-07-26 | localhost enet RTT tune | 78e1d8
static void ng_net_tune_peer(ENetPeer *peer) {
  if (!peer) {
    return;
  }
  peer->roundTripTime = 1;
  peer->roundTripTimeVariance = 0;
  peer->lastRoundTripTime = 1;
  peer->lowestRoundTripTime = 1;
  peer->packetThrottle = ENET_PEER_PACKET_THROTTLE_SCALE;
  peer->packetThrottleLimit = ENET_PEER_PACKET_THROTTLE_SCALE;
  peer->packetThrottleCounter = 0;
  enet_peer_ping_interval(peer, 50);
  /* Keep near ENet defaults. 1s min killed live peers during late-join PHYS
   * reliable bursts (looked like server crash). Graceful close still DISCONNECT. */
  // agent: cursor-grok-4.5 | 2026-07-31 | enet timeout join safe | 0fe3f5
  enet_peer_timeout(peer, 32, 5000, 15000);
  enet_peer_ping(peer);
}

NgNet *ng_net_create(NgNetRole role, const char *host, uint16_t port) {
  if (!ng_net_init_once()) {
    return NULL;
  }

  NgNet *n = (NgNet *)calloc(1, sizeof(NgNet));
  if (!n) {
    if (g_enet_users > 0 && --g_enet_users == 0) {
      enet_deinitialize();
    }
    return NULL;
  }
  n->role = role;

  if (role == NG_NET_ROLE_HOST) {
    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = port;
    n->host = enet_host_create(&addr, 32, 2, 0, 0);
    if (!n->host) {
      NG_LOG_ERROR("enet host create failed (port %u in use?)", port);
      if (g_enet_users > 0 && --g_enet_users == 0) {
        enet_deinitialize();
      }
      free(n);
      return NULL;
    }
    NG_LOG_INFO("net host :%u", port);
  } else if (role == NG_NET_ROLE_CLIENT) {
    n->host = enet_host_create(NULL, 1, 2, 0, 0);
    // agent: composer-2.5 | 2026-07-25 | fix client host create refcount | 8ef74c
    if (!n->host) {
      if (g_enet_users > 0 && --g_enet_users == 0) {
        enet_deinitialize();
      }
      free(n);
      return NULL;
    }
    ENetAddress addr;
    enet_address_set_host(&addr, host ? host : NG_NET_HOST);
    addr.port = port;
    n->peer = enet_host_connect(n->host, &addr, 2, 0);
    NG_LOG_INFO("net client -> %s:%u", host ? host : NG_NET_HOST, port);
  }

  return n;
}

void ng_net_destroy(NgNet *n) {
  if (!n) {
    return;
  }
  if (n->host) {
    // agent: composer-2.5 | 2026-07-25 | graceful client disconnect | c4a9f2
    if (n->role == NG_NET_ROLE_CLIENT && n->peer) {
      enet_peer_disconnect(n->peer, 0);
      ENetEvent ev;
      for (int i = 0; i < 32; i++) {
        if (enet_host_service(n->host, &ev, 50) <= 0) {
          break;
        }
        if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
          break;
        }
      }
    }
    enet_host_flush(n->host);
    enet_host_destroy(n->host);
  }
  free(n);
  if (g_enet_users > 0) {
    g_enet_users--;
    if (g_enet_users == 0) {
      enet_deinitialize();
    }
  }
}

void ng_net_shutdown(void) {}

void ng_net_set_peer_fn(NgNet *n, NgNetPeerFn fn, void *ctx) {
  if (!n) {
    return;
  }
  n->peer_fn = fn;
  n->peer_ctx = ctx;
}

void *ng_net_peer_data(NgNetPeer *peer) {
  if (!peer) {
    return NULL;
  }
  if (peer->lb) {
    return peer->u.lb_data;
  }
  if (!peer->u.peer) {
    return NULL;
  }
  return peer->u.peer->data;
}

void ng_net_peer_set_data(NgNetPeer *peer, void *data) {
  if (!peer) {
    return;
  }
  if (peer->lb) {
    peer->u.lb_data = data;
    return;
  }
  if (!peer->u.peer) {
    return;
  }
  peer->u.peer->data = data;
}

void ng_net_disconnect_peer(NgNet *n, NgNetPeer *peer) {
  // agent: cursor-grok-4.5 | 2026-07-31 | enet disconnect by peer id | 6aeeb9
  (void)n;
  if (!peer || peer->lb || !peer->u.peer) {
    return;
  }
  enet_peer_disconnect(peer->u.peer, 0);
}

// agent: composer-2.5 | 2026-07-25 | verify client peer state | 24b873
bool ng_net_connected(NgNet *n) {
  if (!n) {
    return false;
  }
#if defined(NG_HAS_EMBEDDED)
  if (n->lb) {
    if (n->role == NG_NET_ROLE_HOST) {
      return n->lb_pair && n->lb_pair->linked;
    }
    return n->connected && n->lb_peer;
  }
#endif
  if (n->role == NG_NET_ROLE_HOST) {
    if (!n->host) {
      return false;
    }
    for (size_t i = 0; i < n->host->peerCount; i++) {
      if (ng_net_peer_connected(&n->host->peers[i])) {
        return true;
      }
    }
    return false;
  }
  if (n->role == NG_NET_ROLE_CLIENT) {
    return n->connected && n->peer && ng_net_peer_connected(n->peer);
  }
  return n->connected;
}

void ng_net_flush(NgNet *n) {
#if defined(NG_HAS_EMBEDDED)
  if (n && n->lb) {
    return;
  }
#endif
  if (n && n->host) {
    enet_host_flush(n->host);
  }
}

static bool ng_net_send_peer(ENetPeer *peer, const uint8_t *data, size_t len, uint8_t channel,
                             bool reliable) {
  if (!peer || !data || len == 0) {
    return false;
  }
  ENetPacket *pkt =
      enet_packet_create(data, len, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
  if (!pkt) {
    return false;
  }
  return enet_peer_send(peer, channel, pkt) == 0;
}

bool ng_net_send_to(NgNet *n, NgNetPeer *peer, const uint8_t *data, size_t len,
                    uint8_t channel, bool reliable) {
  if (!n || !data || len == 0) {
    return false;
  }
#if defined(NG_HAS_EMBEDDED)
  if (n->lb && n->lb_pair) {
    NgLoopbackPkt *q;
    int *r;
    int *w;
    if (n->role == NG_NET_ROLE_CLIENT) {
      q = n->lb_pair->to_host;
      r = &n->lb_pair->to_host_r;
      w = &n->lb_pair->to_host_w;
    } else {
      (void)peer;
      q = n->lb_pair->to_client;
      r = &n->lb_pair->to_client_r;
      w = &n->lb_pair->to_client_w;
    }
    const int next = (*w + 1) % NG_LOOPBACK_QCAP;
    if (next == *r || len > NG_LOOPBACK_PKT_MAX) {
      return false;
    }
    memcpy(q[*w].data, data, len);
    q[*w].len = len;
    q[*w].channel = channel;
    *w = next;
    (void)reliable;
    return true;
  }
#endif
  if (n->role == NG_NET_ROLE_CLIENT) {
    if (!n->connected || !n->peer) {
      return false;
    }
    return ng_net_send_peer(n->peer, data, len, channel, reliable);
  }
  if (!peer || !peer->u.peer) {
    return false;
  }
  return ng_net_send_peer(peer->u.peer, data, len, channel, reliable);
}

bool ng_net_send(NgNet *n, const uint8_t *data, size_t len, uint8_t channel, bool reliable) {
  if (!n) {
    return false;
  }
  if (n->role == NG_NET_ROLE_HOST) {
    ng_net_broadcast(n, data, len, channel, reliable);
    return true;
  }
  return ng_net_send_to(n, NULL, data, len, channel, reliable);
}

void ng_net_broadcast(NgNet *n, const uint8_t *data, size_t len, uint8_t channel,
                      bool reliable) {
  if (!n || n->role != NG_NET_ROLE_HOST || !data || len == 0) {
    return;
  }
#if defined(NG_HAS_EMBEDDED)
  if (n->lb) {
    ng_net_send_to(n, &n->lb_pair->host_peer, data, len, channel, reliable);
    return;
  }
#endif
  if (!n->host) {
    return;
  }
  for (size_t i = 0; i < n->host->peerCount; i++) {
    ENetPeer *p = &n->host->peers[i];
    if (ng_net_peer_connected(p)) {
      ng_net_send_peer(p, data, len, channel, reliable);
    }
  }
}

// agent: composer-2.5 | 2026-07-25 | foreach peer iterator | 12ae60
void ng_net_foreach_peer(NgNet *n, NgNetPeerIterFn fn, void *ctx) {
  if (!n || !fn || n->role != NG_NET_ROLE_HOST) {
    return;
  }
#if defined(NG_HAS_EMBEDDED)
  if (n->lb) {
    if (n->lb_pair && n->lb_pair->linked) {
      fn(n, &n->lb_pair->host_peer, ctx);
    }
    return;
  }
#endif
  if (!n->host) {
    return;
  }
  NgNetPeer wrap = {0};
  for (size_t i = 0; i < n->host->peerCount; i++) {
    ENetPeer *p = &n->host->peers[i];
    if (!ng_net_peer_connected(p)) {
      continue;
    }
    wrap.lb = false;
    wrap.u.peer = p;
    fn(n, &wrap, ctx);
  }
}

static void ng_net_dispatch(NgNet *n, NgNetPacketFn fn, void *ctx, ENetEvent *ev) {
  NgNetPeer wrap = {0};
  wrap.lb = false;
  wrap.u.peer = ev->peer;
  switch (ev->type) {
  case ENET_EVENT_TYPE_CONNECT:
    if (n->role == NG_NET_ROLE_CLIENT) {
      n->peer = ev->peer;
      n->connected = true;
    }
    ng_net_tune_peer(ev->peer);
    NG_LOG_INFO("net peer connected");
    if (n->peer_fn) {
      n->peer_fn(n, &wrap, true, n->peer_ctx);
    }
    break;
  case ENET_EVENT_TYPE_RECEIVE:
    fn(n, &wrap, ev->packet->data, ev->packet->dataLength, ev->channelID, ctx);
    enet_packet_destroy(ev->packet);
    break;
  case ENET_EVENT_TYPE_DISCONNECT:
  case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
    /* Timeout (kill -9 / crashed peer) must drop lockstep roster too. */
    // agent: cursor-grok-4.5 | 2026-07-31 | handle enet disconnect timeout | 5247a8
    NG_LOG_INFO("net peer disconnected%s",
                ev->type == ENET_EVENT_TYPE_DISCONNECT_TIMEOUT ? " (timeout)" : "");
    if (n->peer_fn) {
      n->peer_fn(n, &wrap, false, n->peer_ctx);
    }
    if (n->role == NG_NET_ROLE_CLIENT) {
      n->peer = NULL;
      n->connected = false;
    }
    ev->peer->data = NULL;
    break;
  default:
    break;
  }
}

// agent: composer-2.5 | 2026-07-25 | receive first enet poll | af750e
#define NG_NET_DRAIN_MAX 128

static bool ng_net_user_event(const ENetEvent *ev) {
  return ev->type == ENET_EVENT_TYPE_RECEIVE || ev->type == ENET_EVENT_TYPE_CONNECT ||
         ev->type == ENET_EVENT_TYPE_DISCONNECT ||
         ev->type == ENET_EVENT_TYPE_DISCONNECT_TIMEOUT;
}

static void ng_net_service(NgNet *n, NgNetPacketFn fn, void *ctx, uint32_t timeout_ms) {
  ENetEvent ev;
  int other = 0;
  for (;;) {
    const int r = enet_host_service(n->host, &ev, 0);
    if (r <= 0) {
      break;
    }
    ng_net_dispatch(n, fn, ctx, &ev);
    if (ng_net_user_event(&ev)) {
      continue;
    }
    if (++other >= NG_NET_DRAIN_MAX) {
      break;
    }
  }
  if (timeout_ms > 0 && enet_host_service(n->host, &ev, timeout_ms) > 0) {
    ng_net_dispatch(n, fn, ctx, &ev);
  }
}

void ng_net_poll(NgNet *n, NgNetPacketFn fn, void *ctx) {
  if (!n || !fn) {
    return;
  }
#if defined(NG_HAS_EMBEDDED)
  if (n->lb && n->lb_pair) {
    NgLoopbackPkt *q;
    int *r;
    int *w;
    NgNetPeer *from;
    if (n->role == NG_NET_ROLE_HOST) {
      q = n->lb_pair->to_host;
      r = &n->lb_pair->to_host_r;
      w = &n->lb_pair->to_host_w;
      from = &n->lb_pair->client_peer;
    } else {
      q = n->lb_pair->to_client;
      r = &n->lb_pair->to_client_r;
      w = &n->lb_pair->to_client_w;
      from = &n->lb_pair->host_peer;
    }
    // agent: composer-2.5 | 2026-07-25 | loopback drain no stack copy | 99466a
    while (*r != *w) {
      NgLoopbackPkt *pkt = &q[*r];
      *r = (*r + 1) % NG_LOOPBACK_QCAP;
      fn(n, from, pkt->data, pkt->len, pkt->channel, ctx);
    }
    return;
  }
#endif
  if (!n->host) {
    return;
  }
  ng_net_service(n, fn, ctx, 0);
}

void ng_net_poll_wait(NgNet *n, NgNetPacketFn fn, void *ctx, uint32_t timeout_ms) {
  if (!n || !fn) {
    return;
  }
#if defined(NG_HAS_EMBEDDED)
  if (n->lb) {
    ng_net_poll(n, fn, ctx);
    return;
  }
#endif
  if (!n->host) {
    return;
  }
  ng_net_service(n, fn, ctx, timeout_ms);
}

#if defined(NG_HAS_EMBEDDED)
// agent: composer-2.5 | 2026-07-25 | enet defer loopback connect | 3895fa
void ng_net_loopback_connect(NgNetLoopbackPair *pair) {
  if (!pair || pair->linked) {
    return;
  }
  pair->linked = true;
  pair->host->connected = true;
  pair->client->connected = true;
  pair->host_peer.lb = true;
  pair->client_peer.lb = true;
  pair->client->lb_peer = &pair->client_peer;
  NG_LOG_INFO("net loopback connected");
  if (pair->host->peer_fn) {
    pair->host->peer_fn(pair->host, &pair->host_peer, true, pair->host->peer_ctx);
  }
  if (pair->client->peer_fn) {
    pair->client->peer_fn(pair->client, &pair->client_peer, true, pair->client->peer_ctx);
  }
}

NgNetLoopbackPair *ng_net_loopback_create(void) {
  NgNetLoopbackPair *pair = (NgNetLoopbackPair *)calloc(1, sizeof(NgNetLoopbackPair));
  if (!pair) {
    return NULL;
  }
  pair->host = (NgNet *)calloc(1, sizeof(NgNet));
  pair->client = (NgNet *)calloc(1, sizeof(NgNet));
  if (!pair->host || !pair->client) {
    free(pair->host);
    free(pair->client);
    free(pair);
    return NULL;
  }
  pair->host->lb = true;
  pair->host->role = NG_NET_ROLE_HOST;
  pair->host->lb_pair = pair;
  pair->client->lb = true;
  pair->client->role = NG_NET_ROLE_CLIENT;
  pair->client->lb_pair = pair;
  NG_LOG_INFO("net loopback pair ready");
  return pair;
}

void ng_net_loopback_destroy(NgNetLoopbackPair *pair) {
  if (!pair) {
    return;
  }
  if (pair->host && pair->host->peer_fn) {
    pair->host->peer_fn(pair->host, &pair->host_peer, false, pair->host->peer_ctx);
  }
  if (pair->client && pair->client->peer_fn) {
    pair->client->peer_fn(pair->client, &pair->client_peer, false, pair->client->peer_ctx);
  }
  free(pair->host);
  free(pair->client);
  free(pair);
}

NgNet *ng_net_loopback_host(NgNetLoopbackPair *pair) {
  return pair ? pair->host : NULL;
}

NgNet *ng_net_loopback_client(NgNetLoopbackPair *pair) {
  return pair ? pair->client : NULL;
}
#endif
// agent: cursor-grok-4.5 | 2026-07-31 | handle enet disconnect timeout | 5247a8
// agent: cursor-grok-4.5 | 2026-07-31 | enet timeout join safe | 0fe3f5
// agent: cursor-grok-4.5 | 2026-07-31 | enet disconnect by peer id | 6aeeb9
