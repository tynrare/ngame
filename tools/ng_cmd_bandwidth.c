// agent: composer-2.5 | 2026-07-28 | wire bandwidth sample tool | b0c1d2
#include "engine/ng_proto.h"
#include "net/ng_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct BwCtx {
  size_t snap_bytes;
  size_t delta_bytes;
  size_t other_bytes;
  int snap_count;
  int delta_count;
} BwCtx;

static void bw_on_packet(NgNet *net, NgNetPeer *peer, const uint8_t *data, size_t len,
                         uint8_t channel, void *ctx) {
  (void)net;
  (void)peer;
  (void)channel;
  BwCtx *b = (BwCtx *)ctx;
  if (!data || len < sizeof(NgProtoHeader)) {
    return;
  }
  NgProtoHeader h;
  NgProtoBuf buf = {.len = len, .pos = 0};
  memcpy(buf.data, data, len);
  if (!ng_proto_read_header(&buf, &h)) {
    b->other_bytes += len;
    return;
  }
  if (h.type == NG_PKT_SNAPSHOT) {
    b->snap_bytes += len;
    b->snap_count++;
  } else if (h.type == NG_PKT_STATE_UPDATE || h.type == NG_PKT_STATE_BATCH) {
    b->delta_bytes += len;
    b->delta_count++;
  } else if (h.type == NG_PKT_STATE_ACK) {
    b->other_bytes += len;
  } else {
    b->other_bytes += len;
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
    return 1;
  }
  BwCtx bw = {0};
  for (int i = 0; i < 200 && !ng_net_connected(net); i++) {
    ng_net_poll_wait(net, bw_on_packet, &bw, 10);
  }
  if (!ng_net_connected(net)) {
    ng_net_destroy(net);
    ng_net_shutdown();
    return 1;
  }

  const time_t end = time(NULL) + 3;
  while (time(NULL) < end) {
    ng_net_poll_wait(net, bw_on_packet, &bw, 50);
  }

  ng_net_destroy(net);
  ng_net_shutdown();
  printf("BW_SNAP_BYTES %zu COUNT %d\n", bw.snap_bytes, bw.snap_count);
  printf("BW_DELTA_BYTES %zu COUNT %d\n", bw.delta_bytes, bw.delta_count);
  printf("BW_OTHER_BYTES %zu\n", bw.other_bytes);
  return 0;
}
