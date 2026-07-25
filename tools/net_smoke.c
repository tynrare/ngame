// agent: composer-2.5 | 2026-07-25 | ENet smoke test client | n7q95l
// agent: composer-2.5 | 2026-07-25 | wait snapshot before cmd | 1457b1
#include "core/ng_proto.h"
#include "net/ng_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool g_got_reply = false;
static bool g_got_snapshot = false;

static void smoke_on_packet(NgNet *net, NgNetPeer *peer, const uint8_t *data, size_t len,
                            uint8_t channel, void *ctx) {
  (void)net;
  (void)peer;
  (void)channel;
  (void)ctx;
  if (!data || len < sizeof(NgProtoHeader)) {
    return;
  }
  NgProtoBuf buf = {.len = len, .pos = 0};
  memcpy(buf.data, data, len);
  NgProtoHeader h;
  if (!ng_proto_read_header(&buf, &h) || h.magic != NG_PROTO_MAGIC) {
    return;
  }
  if (h.type == NG_PKT_SNAPSHOT) {
    bool delta = false;
    NgSnapshot snap = {0};
    if (ng_proto_decode_snapshot(&buf, &snap, &delta)) {
      printf("SNAPSHOT scene=%s tick=%u entities=%d delta=%d\n", snap.scene_id, snap.tick,
             snap.entity_count, delta ? 1 : 0);
      g_got_snapshot = true;
    }
  } else if (h.type == NG_PKT_CMD_REPLY) {
    char text[1024];
    if (ng_proto_decode_text(&buf, text, sizeof(text))) {
      printf("REPLY: %s\n", text);
      g_got_reply = true;
    }
  } else if (h.type == NG_PKT_EVENT) {
    char text[256];
    if (ng_proto_decode_text(&buf, text, sizeof(text))) {
      printf("EVENT: %s\n", text);
      g_got_reply = true;
    }
  }
}

int main(int argc, char **argv) {
  const char *host = NG_NET_HOST;
  uint16_t port = NG_NET_DEFAULT_PORT;
  if (argc >= 2) {
    host = argv[1];
  }
  if (argc >= 3) {
    port = (uint16_t)atoi(argv[2]);
  }

  NgNet *net = ng_net_create(NG_NET_ROLE_CLIENT, host, port);
  if (!net) {
    fprintf(stderr, "connect failed\n");
    return 1;
  }

// agent: composer-2.5 | 2026-07-25 | poll timeout wait connect | 41a7b1
  for (int i = 0; i < 1000 && !ng_net_connected(net); i++) {
    ng_net_poll_wait(net, smoke_on_packet, NULL, 10);
  }
  if (!ng_net_connected(net)) {
    fprintf(stderr, "timeout waiting for server (is ngame_server running?)\n");
    ng_net_destroy(net);
    ng_net_shutdown();
    return 1;
  }

  for (int i = 0; i < 500 && !g_got_snapshot; i++) {
    ng_net_poll_wait(net, smoke_on_packet, NULL, 10);
  }
  if (!g_got_snapshot) {
    fprintf(stderr, "timeout waiting for snapshot\n");
    ng_net_destroy(net);
    ng_net_shutdown();
    return 1;
  }

  NgProtoBuf cmd;
  if (ng_proto_encode_cmd(&cmd, 1, "scene cube")) {
    ng_net_send(net, cmd.data, cmd.len, NG_CH_RELIABLE, true);
  }

  for (int i = 0; i < 500 && !g_got_reply; i++) {
    ng_net_poll_wait(net, smoke_on_packet, NULL, 10);
  }

  ng_net_destroy(net);
  ng_net_shutdown();
  return g_got_reply ? 0 : 1;
}
