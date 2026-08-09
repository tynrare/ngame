// agent: composer-2.5 | 2026-08-09 | web ws server stubs | 7369e5
#include "ng_ws_server.h"

struct NgWsServer {
  int unused;
};

NgWsServer *ng_ws_server_create(uint16_t port) {
  (void)port;
  return NULL;
}

void ng_ws_server_destroy(NgWsServer *s) { (void)s; }

bool ng_ws_server_poll(NgWsServer *s, NgWsPacketFn fn, void *ctx) {
  (void)s;
  (void)fn;
  (void)ctx;
  return false;
}

bool ng_ws_server_send(NgWsServer *s, const uint8_t *data, size_t len) {
  (void)s;
  (void)data;
  (void)len;
  return false;
}

bool ng_ws_server_connected(NgWsServer *s) {
  (void)s;
  return false;
}

// agent: composer-2.5 | 2026-08-09 | web ws server stubs | 7369e5
