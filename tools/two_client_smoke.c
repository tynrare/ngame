// agent: composer-2.5 | 2026-07-28 | two client session smoke | e7f8a9
// agent: composer-2.5 | 2026-07-28 | fix proto header pos bug | fd457e
// agent: composer-2.5 | 2026-07-28 | rotation relay state update test | 419179
#include "engine/ng_action.h"
#include "engine/ng_proto.h"
#include "engine/ng_session.h"
#include "engine/ng_sync.h"
#include "net/ng_net.h"
#include "world/ng_world.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct SmokeClient {
  NgNet *net;
  bool got_snapshot;
  bool got_cmd_reply;
  bool got_cube_session;
  uint32_t cube_entity_id;
  int state_updates;
  bool got_remote_rot;
  float remote_rot_y;
  uint16_t tx_seq;
} SmokeClient;

static void smoke_note_state(SmokeClient *c, NgProtoBuf *buf) {
  NgStateUpdate update = {0};
  if (!ng_proto_decode_state_update(buf, &update)) {
    return;
  }
  c->state_updates++;
  if (update.comp_mask & NG_COMP_ROT) {
    c->got_remote_rot = true;
    c->remote_rot_y = update.rot[1];
  }
}

static void smoke_on_packet(NgNet *net, NgNetPeer *peer, const uint8_t *data, size_t len,
                            uint8_t channel, void *ctx) {
  (void)net;
  (void)peer;
  (void)channel;
  SmokeClient *c = (SmokeClient *)ctx;
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
    c->got_snapshot = true;
  } else if (h.type == NG_PKT_CMD_REPLY) {
    char text[256];
    if (ng_proto_decode_text(&buf, text, sizeof(text))) {
      c->got_cmd_reply = true;
    }
  } else if (h.type == NG_PKT_ACTION_RESULT) {
    NgActionResult result = {0};
    if (ng_proto_decode_action_result(&buf, &result) && result.reply[0] != '\0') {
      c->got_cmd_reply = true;
    }
  } else if (h.type == NG_PKT_SESSION) {
    NgSessionState session = {.tick = h.tick};
    if (ng_proto_decode_session(&buf, &session) && strcmp(session.scene_id, "cube") == 0) {
      c->got_cube_session = true;
      if (session.spawn_count > 0 && session.spawns[0].entity_id != 0) {
        c->cube_entity_id = session.spawns[0].entity_id;
      }
    }
  } else if (h.type == NG_PKT_STATE_UPDATE) {
    smoke_note_state(c, &buf);
  } else if (h.type == NG_PKT_STATE_BATCH) {
    NgStateUpdate updates[16];
    int count = 0;
    if (ng_proto_decode_state_batch(&buf, updates, 16, &count)) {
      for (int i = 0; i < count; i++) {
        c->state_updates++;
        if (updates[i].comp_mask & NG_COMP_ROT) {
          c->got_remote_rot = true;
          c->remote_rot_y = updates[i].rot[1];
        }
      }
    }
  }
}

static bool smoke_wait_connect(SmokeClient *c, int max_polls) {
  for (int i = 0; i < max_polls && !ng_net_connected(c->net); i++) {
    ng_net_poll_wait(c->net, smoke_on_packet, c, 10);
  }
  return ng_net_connected(c->net);
}

static void smoke_poll(SmokeClient *c, int rounds) {
  for (int i = 0; i < rounds; i++) {
    ng_net_poll_wait(c->net, smoke_on_packet, c, 10);
  }
}

static bool smoke_send_rot_update(SmokeClient *c, uint32_t entity_id, float rot_y) {
  NgStateUpdate update = {
      .entity_id = entity_id,
      .comp_mask = NG_COMP_ROT,
      .rot = {0.0f, rot_y, 0.0f},
      .scale = 1.0f,
  };
  NgProtoBuf pkt;
  if (!ng_proto_encode_state_update(&pkt, ++c->tx_seq, &update)) {
    return false;
  }
  ng_net_send(c->net, pkt.data, pkt.len, NG_CH_UNRELIABLE, false);
  ng_net_flush(c->net);
  return true;
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

  SmokeClient a = {0};
  SmokeClient b = {0};
  a.net = ng_net_create(NG_NET_ROLE_CLIENT, host, port);
  if (!a.net || !smoke_wait_connect(&a, 500)) {
    fprintf(stderr, "two_client_smoke: client A connect failed\n");
    return 1;
  }

  for (int t = 0; t < 500 && !a.got_snapshot; t++) {
    smoke_poll(&a, 1);
  }
  if (!a.got_snapshot) {
    fprintf(stderr, "two_client_smoke: client A snapshot timeout\n");
    return 1;
  }

  NgProtoBuf cmd;
  if (ng_proto_encode_cmd(&cmd, 1, "scene cube")) {
    ng_net_send(a.net, cmd.data, cmd.len, NG_CH_RELIABLE, true);
    ng_net_flush(a.net);
  }
  for (int t = 0; t < 500 && !a.got_cmd_reply; t++) {
    smoke_poll(&a, 1);
  }
  for (int t = 0; t < 500 && !a.got_cube_session; t++) {
    smoke_poll(&a, 1);
  }

  b.net = ng_net_create(NG_NET_ROLE_CLIENT, host, port);
  if (!b.net || !smoke_wait_connect(&b, 500)) {
    fprintf(stderr, "two_client_smoke: client B connect failed\n");
    return 1;
  }
  for (int t = 0; t < 500 && !b.got_snapshot; t++) {
    smoke_poll(&b, 1);
  }
  for (int t = 0; t < 500 && !b.got_cube_session; t++) {
    smoke_poll(&b, 1);
  }

  if (!a.got_cube_session || !b.got_cube_session) {
    fprintf(stderr, "two_client_smoke: missing cube SESSION (A=%d B=%d)\n", a.got_cube_session,
            b.got_cube_session);
    ng_net_destroy(a.net);
    ng_net_destroy(b.net);
    ng_net_shutdown();
    return 1;
  }

  const uint32_t entity_id = a.cube_entity_id ? a.cube_entity_id : b.cube_entity_id;
  if (entity_id == 0) {
    fprintf(stderr, "two_client_smoke: missing cube entity id\n");
    ng_net_destroy(a.net);
    ng_net_destroy(b.net);
    ng_net_shutdown();
    return 1;
  }

  if (!smoke_send_rot_update(&a, entity_id, 0.5f)) {
    fprintf(stderr, "two_client_smoke: failed to send rot update\n");
    ng_net_destroy(a.net);
    ng_net_destroy(b.net);
    ng_net_shutdown();
    return 1;
  }

  for (int t = 0; t < 500 && !b.got_remote_rot; t++) {
    smoke_poll(&a, 1);
    smoke_poll(&b, 1);
  }

  const bool ok = b.got_remote_rot && fabsf(b.remote_rot_y - 0.5f) < 0.02f;
  ng_net_destroy(a.net);
  ng_net_destroy(b.net);
  ng_net_shutdown();

  if (!ok) {
    fprintf(stderr, "two_client_smoke: peer rot sync failed (updates=%d rot=%d y=%.3f)\n",
            b.state_updates, b.got_remote_rot, b.remote_rot_y);
    return 1;
  }
  printf("TWO_CLIENT_SMOKE ok entity=%u rot_y=%.3f\n", entity_id, b.remote_rot_y);
  return 0;
}

// agent: composer-2.5 | 2026-07-28 | fix proto header pos bug | fd457e
// agent: composer-2.5 | 2026-07-28 | rotation relay state update test | 419179
