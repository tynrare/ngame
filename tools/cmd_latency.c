// agent: composer-2.5 | 2026-07-26 | ENet cmd RTT latency test | a3b4c5
#include "core/ng_action.h"
#include "core/ng_proto.h"
#include "net/ng_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define NG_CMD_LATENCY_MAX_MS 100

static double ng_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static bool g_got_snapshot = false;
static bool g_got_action = false;
static double g_t_connect = 0.0;
static double g_t_snapshot = 0.0;
static double g_t_send = 0.0;
static double g_t_action = 0.0;

static void latency_on_packet(NgNet *net, NgNetPeer *peer, const uint8_t *data, size_t len,
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
      if (!g_got_snapshot) {
        g_got_snapshot = true;
        g_t_snapshot = ng_now_ms();
      }
    }
  } else if (h.type == NG_PKT_ACTION_RESULT) {
    NgActionResult result = {0};
    if (ng_proto_decode_action_result(&buf, &result) && result.reply[0] != '\0') {
      if (!g_got_action) {
        g_got_action = true;
        g_t_action = ng_now_ms();
      }
    }
  }
}

// agent: composer-2.5 | 2026-07-26 | poll_wait not usleep | 7f7f04
static void ng_wait_connected(NgNet *net) {
  for (int i = 0; i < 5000 && !ng_net_connected(net); i++) {
    ng_net_poll_wait(net, latency_on_packet, NULL, 1);
  }
}

static void ng_wait_snapshot(NgNet *net) {
  for (int i = 0; i < 5000 && !g_got_snapshot; i++) {
    ng_net_poll_wait(net, latency_on_packet, NULL, 1);
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

  const double t0 = ng_now_ms();
  NgNet *net = ng_net_create(NG_NET_ROLE_CLIENT, host, port);
  if (!net) {
    fprintf(stderr, "cmd_latency: connect failed\n");
    return 1;
  }

  ng_wait_connected(net);
  if (!ng_net_connected(net)) {
    fprintf(stderr, "cmd_latency: timeout waiting for server\n");
    ng_net_destroy(net);
    ng_net_shutdown();
    return 1;
  }
  g_t_connect = ng_now_ms();

  ng_wait_snapshot(net);
  if (!g_got_snapshot) {
    fprintf(stderr, "cmd_latency: timeout waiting for snapshot\n");
    ng_net_destroy(net);
    ng_net_shutdown();
    return 1;
  }

  NgProtoBuf cmd;
  g_t_send = ng_now_ms();
  if (!ng_proto_encode_cmd(&cmd, 1, "scene cube")) {
    fprintf(stderr, "cmd_latency: encode cmd failed\n");
    return 1;
  }
  ng_net_send(net, cmd.data, cmd.len, NG_CH_RELIABLE, true);
  ng_net_flush(net);

  const double deadline = g_t_send + 2000.0;
  while (!g_got_action && ng_now_ms() < deadline) {
    ng_net_poll_wait(net, latency_on_packet, NULL, 1);
  }

  if (!g_got_action) {
    fprintf(stderr, "cmd_latency: timeout waiting for ACTION_RESULT\n");
    ng_net_destroy(net);
    ng_net_shutdown();
    return 1;
  }

  const double cmd_rtt = g_t_action - g_t_send;
  printf("PHASE_MS connect=%.1f snapshot=%.1f cmd=%.1f total=%.1f\n", g_t_connect - t0,
         g_t_snapshot - g_t_connect, cmd_rtt, g_t_action - t0);
  printf("CMD_LATENCY_MS %.3f\n", cmd_rtt);
  if (cmd_rtt > NG_CMD_LATENCY_MAX_MS) {
    fprintf(stderr, "cmd_latency: FAIL %.3f ms > %d ms\n", cmd_rtt, NG_CMD_LATENCY_MAX_MS);
    ng_net_destroy(net);
    ng_net_shutdown();
    return 1;
  }

  ng_net_destroy(net);
  ng_net_shutdown();
  return 0;
}
