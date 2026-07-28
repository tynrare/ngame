// agent: composer-2.5 | 2026-07-27 | action ack without snapshot | c6d7e8
#include "engine/ng_action.h"
#include "server/sim.h"
#include <stdio.h>
#include <string.h>

static NgMsg mod_action_cmd_from_line(const char *line, NgMsg *storage,
                                      const char **argv_buf) {
  NgMsg msg = {
      .kind = NG_MSG_CMD,
      .from = NG_BUS_ANY,
      .to = NG_BUS_SIM,
      .line = line,
  };
  if (line && strncmp(line, "scene ", 6) == 0) {
    argv_buf[0] = "scene";
    argv_buf[1] = line + 6;
    msg.argc = 2;
    msg.argv = argv_buf;
    msg.line = NULL;
  }
  (void)storage;
  return msg;
}

bool ng_action_server_exec(NgWorld *w, const NgMsg *cmd, uint16_t action_seq,
                             NgActionResult *out) {
  if (!w || !cmd || !out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  out->action_seq = action_seq;

  const char *argv_buf[NG_BUS_ARGV_MAX];
  NgMsg work = *cmd;
  if (cmd->line && cmd->argc <= 0) {
    work = mod_action_cmd_from_line(cmd->line, NULL, argv_buf);
  }

  if (!mod_sim_run_cmd(&work, out->reply, sizeof(out->reply))) {
    return false;
  }

  out->server_tick = w->tick;
  out->state_hash = ng_world_hash(w);

  if (work.line && strcmp(work.line, "__agent_snapshot__") == 0) {
    out->kind = NG_ACT_NONE;
    out->have_state = false;
    return true;
  }

  out->have_state = false;
  out->kind = NG_ACT_SCENE_LOAD;
  return true;
}
