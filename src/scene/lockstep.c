// agent: composer-2.5 | 2026-07-29 | lockstep clock module | 4a4ad4
// agent: composer-2.5 | 2026-07-30 | lockstep slot tick tags | bd99f2
#include "lockstep.h"
#include <stdint.h>
#include <string.h>

typedef struct NgLockSlot {
  bool present;
  uint8_t bits;
  uint32_t tick; /* absolute tick; required so ring wrap cannot false-match */
} NgLockSlot;

typedef struct NgLockPeer {
  bool alive;
  uint32_t peer_id;
  NgLockSlot slots[NG_LOCK_RING];
  uint32_t highest_recv;
} NgLockPeer;

typedef struct NgLockCtx {
  bool active;
  bool sim_started;
  uint32_t local_peer_id;
  uint32_t sim_tick;
  uint32_t local_send_tick;
  NgLockPeer peers[NG_LOCK_PEER_MAX];
  int peer_count;
  uint32_t last_hash;
  uint32_t last_hash_tick;
  uint32_t local_ack;
} NgLockCtx;

static NgLockCtx g_lock;

static NgLockPeer *mod_lockstep_find_peer(uint32_t peer_id) {
  for (int i = 0; i < g_lock.peer_count; i++) {
    if (g_lock.peers[i].alive && g_lock.peers[i].peer_id == peer_id) {
      return &g_lock.peers[i];
    }
  }
  return NULL;
}

static bool mod_lockstep_slot_has(const NgLockPeer *p, uint32_t tick) {
  if (!p || tick == 0) {
    return false;
  }
  const NgLockSlot *s = &p->slots[tick % NG_LOCK_RING];
  return s->present && s->tick == tick;
}

static void mod_lockstep_recompute_ack(void) {
  if (g_lock.peer_count == 0) {
    g_lock.local_ack = 0;
    return;
  }
  uint32_t min_h = UINT32_MAX;
  bool any = false;
  for (int i = 0; i < g_lock.peer_count; i++) {
    if (!g_lock.peers[i].alive) {
      continue;
    }
    any = true;
    if (g_lock.peers[i].highest_recv < min_h) {
      min_h = g_lock.peers[i].highest_recv;
    }
  }
  g_lock.local_ack = any ? min_h : 0;
}

static void mod_lockstep_advance_contiguous(NgLockPeer *p) {
  /* At most one ring of progress per call — never spin on wrapped present bits. */
  for (int n = 0; n < NG_LOCK_RING; n++) {
    const uint32_t next = p->highest_recv + 1u;
    if (next == 0u || !mod_lockstep_slot_has(p, next)) {
      break;
    }
    p->highest_recv = next;
  }
  mod_lockstep_recompute_ack();
}

static void mod_lockstep_gen_local(uint32_t tick) {
  if (tick == 0) {
    return;
  }
  NgLockPeer *self = mod_lockstep_find_peer(g_lock.local_peer_id);
  if (!self) {
    mod_lockstep_add_peer(g_lock.local_peer_id);
    self = mod_lockstep_find_peer(g_lock.local_peer_id);
  }
  if (!self) {
    return;
  }
  NgLockSlot *s = &self->slots[tick % NG_LOCK_RING];
  s->present = true;
  s->bits = 0;
  s->tick = tick;
  if (tick > g_lock.local_send_tick) {
    g_lock.local_send_tick = tick;
  }
  mod_lockstep_advance_contiguous(self);
}

static bool mod_lockstep_all_have(uint32_t tick) {
  if (tick == 0) {
    return true;
  }
  if (g_lock.peer_count <= 1) {
    NgLockPeer *self = mod_lockstep_find_peer(g_lock.local_peer_id);
    return self && mod_lockstep_slot_has(self, tick);
  }
  for (int i = 0; i < g_lock.peer_count; i++) {
    NgLockPeer *p = &g_lock.peers[i];
    if (!p->alive) {
      continue;
    }
    if (!mod_lockstep_slot_has(p, tick)) {
      return false;
    }
  }
  return true;
}

void mod_lockstep_reset(void) { memset(&g_lock, 0, sizeof(g_lock)); }

void mod_lockstep_set_active(bool active) {
  if (active && !g_lock.active) {
    g_lock.sim_started = false;
    g_lock.sim_tick = 0;
    g_lock.local_send_tick = 0;
    g_lock.last_hash = 0;
    g_lock.last_hash_tick = 0;
  }
  g_lock.active = active;
}

bool mod_lockstep_active(void) { return g_lock.active; }

void mod_lockstep_set_local_peer(uint32_t peer_id) {
  g_lock.local_peer_id = peer_id ? peer_id : 1u;
  mod_lockstep_add_peer(g_lock.local_peer_id);
}

uint32_t mod_lockstep_local_peer_id(void) { return g_lock.local_peer_id; }

void mod_lockstep_clear_peers(void) {
  memset(g_lock.peers, 0, sizeof(g_lock.peers));
  g_lock.peer_count = 0;
  g_lock.local_ack = 0;
}

void mod_lockstep_add_peer(uint32_t peer_id) {
  if (peer_id == 0 || mod_lockstep_find_peer(peer_id)) {
    return;
  }
  if (g_lock.peer_count >= NG_LOCK_PEER_MAX) {
    return;
  }
  NgLockPeer *p = &g_lock.peers[g_lock.peer_count++];
  memset(p, 0, sizeof(*p));
  p->alive = true;
  p->peer_id = peer_id;
  p->highest_recv = 0;
}

bool mod_lockstep_refuse_late_join(void) {
  return g_lock.active && g_lock.sim_started && g_lock.sim_tick > 0;
}

NgLockGate mod_lockstep_gate(void) {
  // agent: composer-2.5 | 2026-07-30 | solo lockstep always go | 4950ad
  if (!g_lock.active) {
    return NG_LOCK_GATE_GO;
  }
  if (g_lock.local_peer_id == 0) {
    mod_lockstep_set_local_peer(1);
  }
  const uint32_t next_input = g_lock.local_send_tick + 1u;
  mod_lockstep_gen_local(next_input);

  /* Empty-input lockstep (current physics): one peer never waits. */
  if (g_lock.peer_count <= 1) {
    g_lock.sim_started = true;
    return NG_LOCK_GATE_GO;
  }

  if (!g_lock.sim_started) {
    if (g_lock.local_send_tick < (uint32_t)NG_LOCK_PLAYOUT_TICKS) {
      return NG_LOCK_GATE_BUFFER;
    }
    g_lock.sim_started = true;
  }

  const uint32_t next_sim = g_lock.sim_tick + 1u;
  if (!mod_lockstep_all_have(next_sim)) {
    return NG_LOCK_GATE_STALL;
  }
  return NG_LOCK_GATE_GO;
}

void mod_lockstep_on_stepped(uint32_t tick, uint32_t hash) {
  // agent: composer-2.5 | 2026-07-30 | lockstep own sim clock | b39964
  (void)tick;
  g_lock.sim_tick += 1u;
  if (g_lock.sim_tick > 0 && (g_lock.sim_tick % 60u) == 0u) {
    g_lock.last_hash = hash;
    g_lock.last_hash_tick = g_lock.sim_tick;
  }
}

void mod_lockstep_store_remote_input(uint32_t peer_id, uint32_t tick, uint8_t bits) {
  // agent: composer-2.5 | 2026-07-30 | solo lockstep always go | 4950ad
  if (tick == 0 || peer_id == 0) {
    return;
  }
  NgLockPeer *p = mod_lockstep_find_peer(peer_id);
  if (!p) {
    return;
  }
  NgLockSlot *s = &p->slots[tick % NG_LOCK_RING];
  s->present = true;
  s->bits = bits;
  s->tick = tick;
  mod_lockstep_advance_contiguous(p);
}

void mod_lockstep_store_ack(uint32_t peer_id, uint32_t ack_tick) {
  (void)peer_id;
  (void)ack_tick;
}

uint32_t mod_lockstep_sim_tick(void) { return g_lock.sim_tick; }
uint32_t mod_lockstep_local_ack(void) { return g_lock.local_ack; }
uint32_t mod_lockstep_last_hash(void) { return g_lock.last_hash; }
uint32_t mod_lockstep_last_hash_tick(void) { return g_lock.last_hash_tick; }

int mod_lockstep_fill_send_window(uint32_t *out_base_tick, uint8_t *out_bits, int max_count) {
  if (!out_base_tick || !out_bits || max_count <= 0 || g_lock.local_send_tick == 0) {
    return 0;
  }
  NgLockPeer *self = mod_lockstep_find_peer(g_lock.local_peer_id);
  if (!self) {
    return 0;
  }
  uint32_t start = g_lock.local_send_tick > 8u ? (g_lock.local_send_tick - 7u) : 1u;
  if (g_lock.local_ack + 1u > start) {
    start = g_lock.local_ack + 1u;
  }
  if (start > g_lock.local_send_tick) {
    return 0;
  }
  *out_base_tick = start;
  int n = 0;
  for (uint32_t t = start; t <= g_lock.local_send_tick && n < max_count; t++) {
    out_bits[n++] = self->slots[t % NG_LOCK_RING].bits;
  }
  return n;
}

uint32_t mod_lockstep_highest_recv_contiguous(void) { return g_lock.local_ack; }

void mod_lockstep_debug(uint32_t *out_send, int *out_peers, int *out_started, uint32_t *out_peer) {
  if (out_send) {
    *out_send = g_lock.local_send_tick;
  }
  if (out_peers) {
    *out_peers = g_lock.peer_count;
  }
  if (out_started) {
    *out_started = g_lock.sim_started ? 1 : 0;
  }
  if (out_peer) {
    *out_peer = g_lock.local_peer_id;
  }
}

// agent: composer-2.5 | 2026-07-29 | lockstep clock module | 4a4ad4
// agent: composer-2.5 | 2026-07-30 | lockstep own sim clock | b39964
// agent: composer-2.5 | 2026-07-30 | lockstep gate diag | 626c3b
// agent: composer-2.5 | 2026-07-30 | solo lockstep always go | 4950ad
// agent: composer-2.5 | 2026-07-30 | lockstep slot tick tags | bd99f2
