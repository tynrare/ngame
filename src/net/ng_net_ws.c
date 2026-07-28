// agent: composer-2.5 | 2026-07-25 | websocket emscripten transport | e8h06c
#include "ng_net.h"
#include "engine/ng_log.h"

#include <stdlib.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#include <emscripten/websocket.h>
#endif

struct NgNet {
  NgNetRole role;
  bool connected;
#if defined(__EMSCRIPTEN__)
  // agent: composer-2.5 | 2026-07-25 | emscripten websocket handle type | f8b02c
  EMSCRIPTEN_WEBSOCKET_T ws;
  char url[256];
#endif
};

#if defined(__EMSCRIPTEN__)
static NgNet *g_ws_net;
static NgNetPacketFn g_ws_fn;
static void *g_ws_ctx;

static EM_BOOL ng_net_ws_on_message(int event_type, const EmscriptenWebSocketMessageEvent *e,
                                    void *user_data) {
  (void)event_type;
  (void)user_data;
  if (!g_ws_net || !g_ws_fn || !e) {
    return EM_TRUE;
  }
  g_ws_fn(g_ws_net, NULL, (const uint8_t *)e->data, e->numBytes, NG_CH_RELIABLE, g_ws_ctx);
  return EM_TRUE;
}

static EM_BOOL ng_net_ws_on_open(int event_type, const EmscriptenWebSocketOpenEvent *e,
                                 void *user_data) {
  (void)event_type;
  (void)e;
  (void)user_data;
  if (g_ws_net) {
    g_ws_net->connected = true;
  }
  NG_LOG_INFO("websocket connected");
  return EM_TRUE;
}

static EM_BOOL ng_net_ws_on_close(int event_type, const EmscriptenWebSocketCloseEvent *e,
                                  void *user_data) {
  (void)event_type;
  (void)e;
  (void)user_data;
  if (g_ws_net) {
    g_ws_net->connected = false;
  }
  return EM_TRUE;
}
#endif

NgNet *ng_net_create(NgNetRole role, const char *host, uint16_t port) {
#if !defined(__EMSCRIPTEN__)
  (void)role;
  (void)host;
  (void)port;
  NG_LOG_ERROR("websocket transport requires EMSCRIPTEN");
  return NULL;
#else
  if (role != NG_NET_ROLE_CLIENT) {
    NG_LOG_ERROR("websocket transport is client-only");
    return NULL;
  }
  NgNet *n = (NgNet *)calloc(1, sizeof(NgNet));
  if (!n) {
    return NULL;
  }
  n->role = role;
  g_ws_net = n;
  snprintf(n->url, sizeof(n->url), "ws://%s:%u/", host ? host : NG_NET_HOST,
           port ? port : NG_NET_WS_PORT);

  EmscriptenWebSocketCreateAttributes attrs;
  emscripten_websocket_init_create_attributes(&attrs);
  attrs.url = n->url;
  n->ws = emscripten_websocket_new(&attrs);
  // agent: composer-2.5 | 2026-07-25 | websocket create failure check | a26eab
  if (n->ws <= 0) {
    NG_LOG_ERROR("websocket create failed for %s", n->url);
    free(n);
    return NULL;
  }
  emscripten_websocket_set_onopen_callback(n->ws, n, ng_net_ws_on_open);
  emscripten_websocket_set_onmessage_callback(n->ws, n, ng_net_ws_on_message);
  emscripten_websocket_set_onclose_callback(n->ws, n, ng_net_ws_on_close);
  NG_LOG_INFO("websocket client -> %s", n->url);
  return n;
#endif
}

void ng_net_destroy(NgNet *n) {
#if defined(__EMSCRIPTEN__)
  if (n && n->ws) {
    emscripten_websocket_delete(n->ws);
  }
#endif
  free(n);
}

bool ng_net_connected(NgNet *n) { return n && n->connected; }

bool ng_net_send(NgNet *n, const uint8_t *data, size_t len, uint8_t channel,
                 bool reliable) {
  (void)channel;
  (void)reliable;
#if defined(__EMSCRIPTEN__)
  if (!n || !n->connected || !data) {
    return false;
  }
  return emscripten_websocket_send_binary(n->ws, data, (uint32_t)len) == EMSCRIPTEN_RESULT_SUCCESS;
#else
  (void)n;
  (void)data;
  (void)len;
  return false;
#endif
}

void ng_net_poll(NgNet *n, NgNetPacketFn fn, void *ctx) {
#if defined(__EMSCRIPTEN__)
  g_ws_fn = fn;
  g_ws_ctx = ctx;
  (void)n;
#else
  (void)n;
  (void)fn;
  (void)ctx;
#endif
}

void ng_net_poll_wait(NgNet *n, NgNetPacketFn fn, void *ctx, uint32_t timeout_ms) {
  ng_net_poll(n, fn, ctx);
  (void)timeout_ms;
}

void ng_net_shutdown(void) {}
