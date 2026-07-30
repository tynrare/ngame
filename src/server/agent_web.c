// agent: composer-2.5 | 2026-07-30 | web agent stubs | eee292
#include "agent.h"

static bool mod_agent_web_init(void *vctx) {
  (void)vctx;
  return true;
}

static void mod_agent_web_shutdown(void *vctx) { (void)vctx; }

static bool mod_agent_web_on_msg(const NgMsg *msg, void *vctx) {
  (void)msg;
  (void)vctx;
  return false;
}

static const NgModOps g_agent_ops = {
    .name = "agent",
    .dest = NG_BUS_AGENT,
    .side = NG_MOD_SIDE_BOTH,
    .init = mod_agent_web_init,
    .shutdown = mod_agent_web_shutdown,
    .on_msg = mod_agent_web_on_msg,
    .fixed_step = NULL,
};

const NgModOps *mod_agent_ops(void) { return &g_agent_ops; }

void *mod_agent_ctx(void) { return NULL; }

void mod_agent_poll(void) {}

void mod_agent_configure(uint16_t port) { (void)port; }

uint16_t mod_agent_listening_port(void) { return 0; }

// agent: composer-2.5 | 2026-07-30 | web agent stubs | eee292
