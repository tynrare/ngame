// agent: composer-2.5 | 2026-07-25 | ENet smoke test client | n7q95l
// agent: composer-2.5 | 2026-07-25 | smoke ACTION_RESULT decode | b1c2d3
#include "engine/ng_action.h"
#include "engine/ng_proto.h"
#include "engine/ng_session.h"
#include "engine/ng_sync.h"
#include "net/ng_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool g_got_cmd_reply = false;
static bool g_got_cube_session = false;
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
  } else if (h.type == NG_PKT_ACTION_RESULT) {
    NgActionResult result = {0};
    if (ng_proto_decode_action_result(&buf, &result)) {
      if (result.have_state) {
        printf("ACTION scene=%s tick=%u hash=%u\n", result.state.scene_id, result.server_tick,
               result.state_hash);
      }
      if (result.reply[0] != '\0') {
        printf("REPLY: %s\n", result.reply);
        g_got_cmd_reply = true;
      }
    }
  } else if (h.type == NG_PKT_SESSION) {
    NgSessionState session = {.tick = h.tick};
    if (ng_proto_decode_session(&buf, &session)) {
      session.tick = h.tick;
      printf("SESSION scene=%s controller=%u you=%u sync=%s spawns=%d\n",
             session.scene_id, session.controller_id, session.your_id,
             ng_sync_mode_name(session.scene_sync), session.spawn_count);
      if (strcmp(session.scene_id, "cube") == 0) {
        g_got_cube_session = true;
        if (session.spawn_count >= 1 && session.spawns[0].sync != NG_SYNC_SHARED) {
          fprintf(stderr, "expected cube spawn sync=shared got %s\n",
                  ng_sync_mode_name(session.spawns[0].sync));
          exit(1);
        }
      }
    }
  } else if (h.type == NG_PKT_CMD_REPLY) {
    char text[1024];
    if (ng_proto_decode_text(&buf, text, sizeof(text))) {
      printf("REPLY: %s\n", text);
      g_got_cmd_reply = true;
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
    ng_net_flush(net);
  }

  for (int i = 0; i < 500 && !g_got_cmd_reply; i++) {
    ng_net_poll_wait(net, smoke_on_packet, NULL, 10);
  }
  if (!g_got_cmd_reply) {
    fprintf(stderr, "timeout waiting for cmd reply\n");
    ng_net_destroy(net);
    ng_net_shutdown();
    return 1;
  }

  for (int i = 0; i < 500 && !g_got_cube_session; i++) {
    ng_net_poll_wait(net, smoke_on_packet, NULL, 10);
  }
  if (!g_got_cube_session) {
    fprintf(stderr, "timeout waiting for cube SESSION\n");
    ng_net_destroy(net);
    ng_net_shutdown();
    return 1;
  }

  ng_net_destroy(net);
  ng_net_shutdown();
  return 0;
}
