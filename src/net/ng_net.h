// agent: composer-2.5 | 2026-07-25 | network transport API | c6f84a
#ifndef NG_NET_H
#define NG_NET_H

#include "core/ng_proto.h"
#include <stdbool.h>
#include <stdint.h>

#define NG_NET_DEFAULT_PORT 27015
#define NG_NET_HOST "127.0.0.1"

#define NG_NET_WS_PORT 27016

typedef enum NgNetRole {
  NG_NET_ROLE_NONE = 0,
  NG_NET_ROLE_HOST,
  NG_NET_ROLE_CLIENT,
} NgNetRole;

typedef struct NgNet NgNet;
typedef struct NgNetPeer NgNetPeer;

typedef void (*NgNetPacketFn)(NgNet *n, NgNetPeer *peer, const uint8_t *data, size_t len,
                              uint8_t channel, void *ctx);
typedef void (*NgNetPeerFn)(NgNet *n, NgNetPeer *peer, bool connected, void *ctx);
typedef void (*NgNetPeerIterFn)(NgNet *n, NgNetPeer *peer, void *ctx);

void ng_net_destroy(NgNet *n);
void ng_net_shutdown(void);

NgNet *ng_net_create(NgNetRole role, const char *host, uint16_t port);
bool ng_net_connected(NgNet *n);
void ng_net_flush(NgNet *n);
void ng_net_set_peer_fn(NgNet *n, NgNetPeerFn fn, void *ctx);
void *ng_net_peer_data(NgNetPeer *peer);
void ng_net_peer_set_data(NgNetPeer *peer, void *data);
bool ng_net_send(NgNet *n, const uint8_t *data, size_t len, uint8_t channel, bool reliable);
bool ng_net_send_to(NgNet *n, NgNetPeer *peer, const uint8_t *data, size_t len,
                    uint8_t channel, bool reliable);
void ng_net_broadcast(NgNet *n, const uint8_t *data, size_t len, uint8_t channel,
                      bool reliable);
void ng_net_poll(NgNet *n, NgNetPacketFn fn, void *ctx);
void ng_net_poll_wait(NgNet *n, NgNetPacketFn fn, void *ctx, uint32_t timeout_ms);
void ng_net_foreach_peer(NgNet *n, NgNetPeerIterFn fn, void *ctx);

#if defined(NG_HAS_EMBEDDED) || defined(NG_EMBEDDED)
typedef struct NgNetLoopbackPair NgNetLoopbackPair;
NgNetLoopbackPair *ng_net_loopback_create(void);
// agent: composer-2.5 | 2026-07-25 | export loopback connect API | 44b6a7
void ng_net_loopback_connect(NgNetLoopbackPair *pair);
void ng_net_loopback_destroy(NgNetLoopbackPair *pair);
NgNet *ng_net_loopback_host(NgNetLoopbackPair *pair);
NgNet *ng_net_loopback_client(NgNetLoopbackPair *pair);
#endif

#endif
