// agent: composer-2.5 | 2026-07-25 | websocket server bridge | m6p84k
#ifndef NG_WS_SERVER_H
#define NG_WS_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*NgWsPacketFn)(const uint8_t *data, size_t len, void *ctx);

typedef struct NgWsServer NgWsServer;

NgWsServer *ng_ws_server_create(uint16_t port);
void ng_ws_server_destroy(NgWsServer *s);
bool ng_ws_server_poll(NgWsServer *s, NgWsPacketFn fn, void *ctx);
bool ng_ws_server_send(NgWsServer *s, const uint8_t *data, size_t len);
bool ng_ws_server_connected(NgWsServer *s);

#endif
