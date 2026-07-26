// agent: composer-2.5 | 2026-07-25 | in-process loopback net | 8dc143
// agent: composer-2.5 | 2026-07-25 | in-process loopback net | 8dc143
#include "ng_net.h"
#include "core/ng_log.h"
#include <stdlib.h>
#include <string.h>

#define NG_LOOPBACK_QCAP 16
#define NG_LOOPBACK_PKT_MAX 8192

typedef struct NgLoopbackPkt {
  uint8_t data[NG_LOOPBACK_PKT_MAX];
  size_t len;
  uint8_t channel;
} NgLoopbackPkt;

typedef struct NgLbPeer {
  void *tag;
  void *data;
} NgLbPeer;

struct NgNetLoopbackPair {
  NgLoopbackPkt to_host[NG_LOOPBACK_QCAP];
  int to_host_r;
  int to_host_w;
  NgLoopbackPkt to_client[NG_LOOPBACK_QCAP];
  int to_client_r;
  int to_client_w;
  bool linked;
  struct NgNet *host;
  struct NgNet *client;
  NgLbPeer host_peer;
  NgLbPeer client_peer;
};

struct NgNet {
  NgNetRole role;
  NgNetLoopbackPair *pair;
  bool connected;
  NgNetPeerFn peer_fn;
  void *peer_ctx;
  NgNetPeer *peer;
};

static bool ng_loopback_push(NgLoopbackPkt *q, int *r, int *w, const uint8_t *data, size_t len,
                             uint8_t channel) {
  if (!data || len == 0 || len > NG_LOOPBACK_PKT_MAX) {
    return false;
  }
  const int next = (*w + 1) % NG_LOOPBACK_QCAP;
  if (next == *r) {
    return false;
  }
  memcpy(q[*w].data, data, len);
  q[*w].len = len;
  q[*w].channel = channel;
  *w = next;
  return true;
}

static bool ng_loopback_pop(NgLoopbackPkt *q, int *r, int *w, NgLoopbackPkt **out) {
  if (*r == *w) {
    return false;
  }
  *out = &q[*r];
  *r = (*r + 1) % NG_LOOPBACK_QCAP;
  return true;
}

static void ng_loopback_connect(NgNetLoopbackPair *pair) {
  if (!pair || pair->linked) {
    return;
  }
  pair->linked = true;
  pair->host->connected = true;
  pair->client->connected = true;
  pair->client->peer = (NgNetPeer *)&pair->client_peer;
  NG_LOG_INFO("net loopback connected");
  if (pair->host->peer_fn) {
    pair->host->peer_fn(pair->host, (NgNetPeer *)&pair->host_peer, true, pair->host->peer_ctx);
  }
  if (pair->client->peer_fn) {
    pair->client->peer_fn(pair->client, (NgNetPeer *)&pair->client_peer, true,
                          pair->client->peer_ctx);
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

  pair->host->role = NG_NET_ROLE_HOST;
  pair->host->pair = pair;
  pair->client->role = NG_NET_ROLE_CLIENT;
  pair->client->pair = pair;
  pair->host_peer.tag = (void *)0x1;
  pair->client_peer.tag = (void *)0x2;
  NG_LOG_INFO("net loopback pair ready");
  return pair;
}

void ng_net_loopback_connect(NgNetLoopbackPair *pair) { ng_loopback_connect(pair); }

void ng_net_loopback_destroy(NgNetLoopbackPair *pair) {
  if (!pair) {
    return;
  }
  if (pair->host && pair->host->peer_fn) {
    pair->host->peer_fn(pair->host, (NgNetPeer *)&pair->host_peer, false, pair->host->peer_ctx);
  }
  if (pair->client && pair->client->peer_fn) {
    pair->client->peer_fn(pair->client, (NgNetPeer *)&pair->client_peer, false,
                          pair->client->peer_ctx);
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

NgNet *ng_net_create(NgNetRole role, const char *host, uint16_t port) {
  (void)role;
  (void)host;
  (void)port;
  NG_LOG_ERROR("ng_net_create unavailable in loopback-only build");
  return NULL;
}

void ng_net_destroy(NgNet *n) { (void)n; }

void ng_net_shutdown(void) {}

void ng_net_set_peer_fn(NgNet *n, NgNetPeerFn fn, void *ctx) {
  if (!n) {
    return;
  }
  n->peer_fn = fn;
  n->peer_ctx = ctx;
}

void *ng_net_peer_data(NgNetPeer *peer) {
  return peer ? ((NgLbPeer *)peer)->data : NULL;
}

void ng_net_peer_set_data(NgNetPeer *peer, void *data) {
  if (peer) {
    ((NgLbPeer *)peer)->data = data;
  }
}

bool ng_net_connected(NgNet *n) {
  if (!n || !n->pair) {
    return false;
  }
  if (n->role == NG_NET_ROLE_HOST) {
    return n->pair->linked;
  }
  return n->connected && n->peer;
}

void ng_net_flush(NgNet *n) { (void)n; }

bool ng_net_send_to(NgNet *n, NgNetPeer *peer, const uint8_t *data, size_t len, uint8_t channel,
                    bool reliable) {
  (void)peer;
  (void)reliable;
  if (!n || !n->pair || !data || len == 0) {
    return false;
  }
  if (n->role == NG_NET_ROLE_CLIENT) {
    return ng_loopback_push(n->pair->to_host, &n->pair->to_host_r, &n->pair->to_host_w, data, len,
                            channel);
  }
  return ng_loopback_push(n->pair->to_client, &n->pair->to_client_r, &n->pair->to_client_w, data,
                          len, channel);
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

void ng_net_broadcast(NgNet *n, const uint8_t *data, size_t len, uint8_t channel, bool reliable) {
  if (!n || n->role != NG_NET_ROLE_HOST || !n->pair || !data || len == 0) {
    return;
  }
  ng_net_send_to(n, (NgNetPeer *)&n->pair->host_peer, data, len, channel, reliable);
}

void ng_net_foreach_peer(NgNet *n, NgNetPeerIterFn fn, void *ctx) {
  if (!n || !fn || n->role != NG_NET_ROLE_HOST || !n->pair || !n->pair->linked) {
    return;
  }
  fn(n, (NgNetPeer *)&n->pair->host_peer, ctx);
}

// agent: composer-2.5 | 2026-07-25 | loopback pop by pointer | f69394
static void ng_loopback_drain(NgNet *n, NgLoopbackPkt *q, int *r, int *w, NgLbPeer *from,
                              NgNetPacketFn fn, void *ctx) {
  NgLoopbackPkt *pkt;
  while (ng_loopback_pop(q, r, w, &pkt)) {
    fn(n, (NgNetPeer *)from, pkt->data, pkt->len, pkt->channel, ctx);
  }
}

void ng_net_poll(NgNet *n, NgNetPacketFn fn, void *ctx) {
  if (!n || !n->pair || !fn) {
    return;
  }
  if (n->role == NG_NET_ROLE_HOST) {
    ng_loopback_drain(n, n->pair->to_host, &n->pair->to_host_r, &n->pair->to_host_w,
                      &n->pair->client_peer, fn, ctx);
  } else {
    ng_loopback_drain(n, n->pair->to_client, &n->pair->to_client_r, &n->pair->to_client_w,
                      &n->pair->host_peer, fn, ctx);
  }
}

void ng_net_poll_wait(NgNet *n, NgNetPacketFn fn, void *ctx, uint32_t timeout_ms) {
  (void)timeout_ms;
  ng_net_poll(n, fn, ctx);
}
