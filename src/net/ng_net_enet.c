// agent: composer-2.5 | 2026-07-25 | ENet multi-peer transport | d7g95b
#include "ng_net.h"
#include "core/ng_log.h"

#define ENET_IMPLEMENTATION
#include "vendor/enet.h"

#include <stdlib.h>
#include <string.h>

struct NgNetPeer {
  ENetPeer *peer;
};

struct NgNet {
  NgNetRole role;
  ENetHost *host;
  ENetPeer *peer;
  bool connected;
  NgNetPeerFn peer_fn;
  void *peer_ctx;
};

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
  if (!peer || !peer->peer) {
    return NULL;
  }
  return peer->peer->data;
}

void ng_net_peer_set_data(NgNetPeer *peer, void *data) {
  if (!peer || !peer->peer) {
    return;
  }
  peer->peer->data = data;
}

// agent: composer-2.5 | 2026-07-25 | verify client peer state | 24b873
bool ng_net_connected(NgNet *n) {
  if (!n) {
    return false;
  }
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
  if (n->role == NG_NET_ROLE_CLIENT) {
    if (!n->connected || !n->peer) {
      return false;
    }
    return ng_net_send_peer(n->peer, data, len, channel, reliable);
  }
  if (!peer || !peer->peer) {
    return false;
  }
  return ng_net_send_peer(peer->peer, data, len, channel, reliable);
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
  if (!n || n->role != NG_NET_ROLE_HOST || !n->host || !data || len == 0) {
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
  if (!n || !fn || n->role != NG_NET_ROLE_HOST || !n->host) {
    return;
  }
  NgNetPeer wrap = {0};
  for (size_t i = 0; i < n->host->peerCount; i++) {
    ENetPeer *p = &n->host->peers[i];
    if (!ng_net_peer_connected(p)) {
      continue;
    }
    wrap.peer = p;
    fn(n, &wrap, ctx);
  }
}

static void ng_net_dispatch(NgNet *n, NgNetPacketFn fn, void *ctx, ENetEvent *ev) {
  NgNetPeer wrap = {0};
  wrap.peer = ev->peer;
  switch (ev->type) {
  case ENET_EVENT_TYPE_CONNECT:
    if (n->role == NG_NET_ROLE_CLIENT) {
      n->peer = ev->peer;
      n->connected = true;
    }
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
    NG_LOG_INFO("net peer disconnected");
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

// agent: composer-2.5 | 2026-07-25 | receive priority enet poll | 79008c
#define NG_NET_DRAIN_MAX 128

static void ng_net_service(NgNet *n, NgNetPacketFn fn, void *ctx, uint32_t timeout_ms) {
  ENetEvent ev;
  int drained = 0;
  while (drained < NG_NET_DRAIN_MAX && enet_host_service(n->host, &ev, 0) > 0) {
    ng_net_dispatch(n, fn, ctx, &ev);
    drained++;
  }
  if (timeout_ms > 0 && enet_host_service(n->host, &ev, timeout_ms) > 0) {
    ng_net_dispatch(n, fn, ctx, &ev);
  }
}

void ng_net_poll(NgNet *n, NgNetPacketFn fn, void *ctx) {
  if (!n || !n->host || !fn) {
    return;
  }
  ng_net_service(n, fn, ctx, 0);
}

void ng_net_poll_wait(NgNet *n, NgNetPacketFn fn, void *ctx, uint32_t timeout_ms) {
  if (!n || !n->host || !fn) {
    return;
  }
  ng_net_service(n, fn, ctx, timeout_ms);
}
