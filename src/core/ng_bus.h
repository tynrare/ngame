// agent: composer-2.5 | 2026-07-25 | message bus router | 4a8c1e
#ifndef NG_BUS_H
#define NG_BUS_H

#include "world/ng_world.h"
#include <stdbool.h>
#include <stddef.h>

#define NG_BUS_ARGV_MAX 16
#define NG_BUS_DEST_MAX 8

typedef enum NgBusDest {
  NG_BUS_ANY = 0,
  NG_BUS_CONSOLE,
  NG_BUS_SCRIPT,
  NG_BUS_SCENE,
  NG_BUS_SIM,
  NG_BUS_NET,
  NG_BUS_RENDER,
  NG_BUS_AGENT,
} NgBusDest;

typedef enum NgMsgKind {
  NG_MSG_CMD,
  NG_MSG_REPLY,
  NG_MSG_TICK,
  NG_MSG_DRAW,
  NG_MSG_SHUTDOWN,
  NG_MSG_INPUT,
  NG_MSG_SNAPSHOT,
  NG_MSG_EVENT,
} NgMsgKind;

typedef struct NgMsg {
  NgMsgKind kind;
  NgBusDest from;
  NgBusDest to;
  const char *line;
  int argc;
  const char **argv;
  const char *text;
  float dt;
  int input_buttons;
  float input_yaw_delta;
  uint16_t input_seq;
  const NgSnapshot *snapshot;
} NgMsg;

typedef bool (*NgBusHandler)(const NgMsg *msg, void *ctx);

void ng_bus_init(void);
void ng_bus_shutdown(void);
NgBusDest ng_bus_dest_from_string(const char *name);
void ng_bus_subscribe(NgBusDest dest, NgBusHandler handler, void *ctx);
bool ng_bus_publish(NgMsg *msg);
void ng_bus_set_gate(NgBusDest dest, bool enabled);
bool ng_bus_gate(NgBusDest dest);

#endif
