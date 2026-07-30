// agent: composer-2.5 | 2026-07-29 | lockstep clock module | 4a4ad4
// agent: composer-2.5 | 2026-07-30 | lockstep slot tick tags | bd99f2
// agent: composer-2.5 | 2026-07-30 | lockstep syncing gate APIs | ae43d3
// agent: composer-2.5 | 2026-07-30 | capture buttons into lock slots | 60a1ec
// agent: composer-2.5 | 2026-07-30 | bits_for and step tick APIs | a119d0
// agent: composer-2.5 | 2026-07-30 | ack trims send window | 0bb3f0
// agent: composer-2.5 | 2026-07-30 | slot conflict triggers desync | b2989d
// agent: composer-2.5 | 2026-07-30 | remove peer on disconnect | 98511c
// agent: composer-2.5 | 2026-07-30 | hash mismatch stalls sync | abbc70
#include "lockstep.h"
#include "client/input.h"
#include "engine/ng_log.h"
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
  uint32_t ack_our; /* remote reports they have world inputs through this tick */
} NgLockPeer;

typedef struct NgLockCtx {
  bool active;
  bool sim_started;
  bool syncing;
  bool await_phys;
  uint32_t local_peer_id;
  uint32_t sim_tick;
  uint32_t step_tick;
  uint32_t local_send_tick;
  uint32_t playout_ticks;
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
  // agent: composer-2.5 | 2026-07-30 | capture buttons into lock slots | 60a1ec
  // agent: composer-2.5 | 2026-07-30 | slot conflict triggers desync | b2989d
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
  const uint8_t bits = (uint8_t)(mod_input_buttons() & 0xff);
  if (s->present && s->tick == tick) {
    /* Already committed for this tick — do not change bits. */
    if (tick > g_lock.local_send_tick) {
      g_lock.local_send_tick = tick;
    }
    return;
  }
  s->present = true;
  s->bits = bits;
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

void mod_lockstep_reset(void) {
  // agent: composer-2.5 | 2026-07-30 | lockstep playout setter | 9b7f42
  memset(&g_lock, 0, sizeof(g_lock));
  g_lock.playout_ticks = (uint32_t)NG_LOCK_PLAYOUT_TICKS;
}

void mod_lockstep_set_playout_ticks(uint32_t ticks) {
  if (ticks < 1u) {
    ticks = 1u;
  }
  if (ticks > (uint32_t)NG_LOCK_RING / 2u) {
    ticks = (uint32_t)NG_LOCK_RING / 2u;
  }
  g_lock.playout_ticks = ticks;
}

uint32_t mod_lockstep_playout_ticks(void) {
  return g_lock.playout_ticks ? g_lock.playout_ticks : (uint32_t)NG_LOCK_PLAYOUT_TICKS;
}

void mod_lockstep_set_active(bool active) {
  // agent: composer-2.5 | 2026-07-30 | lockstep join fixes logging | 5d65f6
  if (active && !g_lock.active) {
    /* Do not wipe clock while a late-join sync is in flight. */
    if (!g_lock.syncing && !g_lock.await_phys) {
      g_lock.sim_started = false;
      g_lock.sim_tick = 0;
      g_lock.step_tick = 0;
      g_lock.local_send_tick = 0;
      g_lock.last_hash = 0;
      g_lock.last_hash_tick = 0;
    }
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
  p->ack_our = 0;
}

void mod_lockstep_remove_peer(uint32_t peer_id) {
  // agent: composer-2.5 | 2026-07-30 | remove peer on disconnect | 98511c
  if (peer_id == 0 || peer_id == g_lock.local_peer_id) {
    return;
  }
  for (int i = 0; i < g_lock.peer_count; i++) {
    if (!g_lock.peers[i].alive || g_lock.peers[i].peer_id != peer_id) {
      continue;
    }
    g_lock.peers[i] = g_lock.peers[g_lock.peer_count - 1];
    memset(&g_lock.peers[g_lock.peer_count - 1], 0, sizeof(g_lock.peers[0]));
    g_lock.peer_count--;
    mod_lockstep_recompute_ack();
    NG_LOG_INFO("lockstep: removed peer=%u count=%d", peer_id, g_lock.peer_count);
    return;
  }
}

bool mod_lockstep_needs_join_sync(void) {
  /* Mid-sim join once the lockstep clock has advanced at least one step. */
  return g_lock.active && g_lock.sim_tick > 0;
}

void mod_lockstep_begin_sync(uint32_t tick) {
  g_lock.syncing = true;
  if (tick > 0) {
    g_lock.sim_tick = tick;
  }
}

void mod_lockstep_end_sync(void) {
  g_lock.syncing = false;
  g_lock.await_phys = false;
  g_lock.sim_started = true;
  /* Align send clock so next inputs continue from sim_tick. */
  g_lock.local_send_tick = g_lock.sim_tick;
  for (int i = 0; i < g_lock.peer_count; i++) {
    if (g_lock.peers[i].alive) {
      g_lock.peers[i].highest_recv = g_lock.sim_tick;
      g_lock.peers[i].ack_our = g_lock.sim_tick;
      memset(g_lock.peers[i].slots, 0, sizeof(g_lock.peers[i].slots));
    }
  }
  mod_lockstep_recompute_ack();
}

bool mod_lockstep_syncing(void) { return g_lock.syncing; }

void mod_lockstep_set_sim_tick(uint32_t tick) { g_lock.sim_tick = tick; }

void mod_lockstep_await_phys(bool await) { g_lock.await_phys = await; }

bool mod_lockstep_awaiting_phys(void) { return g_lock.await_phys; }

void mod_lockstep_note_desync(void) {
  // agent: composer-2.5 | 2026-07-30 | hash mismatch stalls sync | abbc70
  NG_LOG_ERROR("lockstep: desync — stalling at tick=%u", g_lock.sim_tick);
  mod_lockstep_begin_sync(g_lock.sim_tick);
}

NgLockGate mod_lockstep_gate(void) {
  // agent: composer-2.5 | 2026-07-30 | solo lockstep always go | 4950ad
  // agent: composer-2.5 | 2026-07-30 | gate capture next sim tick | ba4520
  if (!g_lock.active) {
    return NG_LOCK_GATE_GO;
  }
  if (g_lock.syncing || g_lock.await_phys) {
    return NG_LOCK_GATE_STALL;
  }
  if (g_lock.local_peer_id == 0) {
    mod_lockstep_set_local_peer(1);
  }

  const uint32_t next_sim = g_lock.sim_tick + 1u;
  /* Always sample/commit bits for the tick about to step. */
  mod_lockstep_gen_local(next_sim);

  /* Solo lockstep: one peer never waits. */
  if (g_lock.peer_count <= 1) {
    g_lock.sim_started = true;
    return NG_LOCK_GATE_GO;
  }

  /* Multi-peer: fill send-ahead for playout without changing next_sim. */
  const uint32_t playout = mod_lockstep_playout_ticks();
  const uint32_t send_target = g_lock.sim_tick + playout;
  while (g_lock.local_send_tick < send_target) {
    mod_lockstep_gen_local(g_lock.local_send_tick + 1u);
  }

  if (!g_lock.sim_started) {
    if (g_lock.local_send_tick < playout) {
      return NG_LOCK_GATE_BUFFER;
    }
    g_lock.sim_started = true;
  }

  if (!mod_lockstep_all_have(next_sim)) {
    return NG_LOCK_GATE_STALL;
  }
  return NG_LOCK_GATE_GO;
}

void mod_lockstep_on_stepped(uint32_t tick, uint32_t hash) {
  // agent: composer-2.5 | 2026-07-30 | lockstep own sim clock | b39964
  (void)tick;
  g_lock.sim_tick += 1u;
  g_lock.step_tick = 0;
  if (g_lock.sim_tick > 0 && (g_lock.sim_tick % 60u) == 0u) {
    g_lock.last_hash = hash;
    g_lock.last_hash_tick = g_lock.sim_tick;
  }
}

void mod_lockstep_store_remote_input(uint32_t peer_id, uint32_t tick, uint8_t bits) {
  // agent: composer-2.5 | 2026-07-30 | solo lockstep always go | 4950ad
  // agent: composer-2.5 | 2026-07-30 | slot conflict triggers desync | b2989d
  if (tick == 0 || peer_id == 0) {
    return;
  }
  NgLockPeer *p = mod_lockstep_find_peer(peer_id);
  if (!p) {
    return;
  }
  NgLockSlot *s = &p->slots[tick % NG_LOCK_RING];
  if (s->present && s->tick == tick) {
    if (s->bits != bits) {
      NG_LOG_ERROR("lockstep: input conflict peer=%u tick=%u got=%u have=%u", peer_id, tick,
                   (unsigned)bits, (unsigned)s->bits);
      mod_lockstep_note_desync();
    }
    return;
  }
  s->present = true;
  s->bits = bits;
  s->tick = tick;
  mod_lockstep_advance_contiguous(p);
}

void mod_lockstep_store_ack(uint32_t peer_id, uint32_t ack_tick) {
  // agent: composer-2.5 | 2026-07-30 | ack trims send window | 0bb3f0
  if (peer_id == 0 || peer_id == g_lock.local_peer_id) {
    return;
  }
  NgLockPeer *p = mod_lockstep_find_peer(peer_id);
  if (!p) {
    return;
  }
  if (ack_tick > p->ack_our) {
    p->ack_our = ack_tick;
  }
}

uint32_t mod_lockstep_sim_tick(void) { return g_lock.sim_tick; }

uint32_t mod_lockstep_step_tick(void) {
  return g_lock.step_tick ? g_lock.step_tick : (g_lock.sim_tick + 1u);
}

void mod_lockstep_set_step_tick(uint32_t tick) { g_lock.step_tick = tick; }

uint8_t mod_lockstep_bits_for(uint32_t peer_id, uint32_t tick) {
  // agent: composer-2.5 | 2026-07-30 | bits_for and step tick APIs | a119d0
  if (tick == 0 || peer_id == 0) {
    return 0;
  }
  NgLockPeer *p = mod_lockstep_find_peer(peer_id);
  if (!p || !mod_lockstep_slot_has(p, tick)) {
    return 0;
  }
  return p->slots[tick % NG_LOCK_RING].bits;
}

bool mod_lockstep_have_input(uint32_t peer_id, uint32_t tick) {
  // agent: composer-2.5 | 2026-07-30 | peer count accessor | ea5827
  if (tick == 0 || peer_id == 0) {
    return false;
  }
  NgLockPeer *p = mod_lockstep_find_peer(peer_id);
  return p && mod_lockstep_slot_has(p, tick);
}

int mod_lockstep_peer_count(void) {
  // agent: composer-2.5 | 2026-07-30 | peer count accessor | ea5827
  return g_lock.peer_count;
}

uint32_t mod_lockstep_local_ack(void) { return g_lock.local_ack; }
uint32_t mod_lockstep_last_hash(void) { return g_lock.last_hash; }
uint32_t mod_lockstep_last_hash_tick(void) { return g_lock.last_hash_tick; }

int mod_lockstep_fill_send_window(uint32_t *out_base_tick, uint8_t *out_bits, int max_count) {
  // agent: composer-2.5 | 2026-07-30 | ack trims send window | 0bb3f0
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
  /* Trim using remotes' ack of world inputs (includes our stream). */
  uint32_t min_remote_ack = UINT32_MAX;
  bool any_remote = false;
  for (int i = 0; i < g_lock.peer_count; i++) {
    NgLockPeer *p = &g_lock.peers[i];
    if (!p->alive || p->peer_id == g_lock.local_peer_id) {
      continue;
    }
    any_remote = true;
    if (p->ack_our < min_remote_ack) {
      min_remote_ack = p->ack_our;
    }
  }
  if (any_remote && min_remote_ack + 1u > start) {
    start = min_remote_ack + 1u;
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

void mod_lockstep_debug_full(uint32_t *out_send, int *out_peers, int *out_started, uint32_t *out_peer,
                             int *out_syncing, int *out_await, uint32_t *out_sim) {
  mod_lockstep_debug(out_send, out_peers, out_started, out_peer);
  if (out_syncing) {
    *out_syncing = g_lock.syncing ? 1 : 0;
  }
  if (out_await) {
    *out_await = g_lock.await_phys ? 1 : 0;
  }
  if (out_sim) {
    *out_sim = g_lock.sim_tick;
  }
}

// agent: composer-2.5 | 2026-07-29 | lockstep clock module | 4a4ad4
// agent: composer-2.5 | 2026-07-30 | lockstep own sim clock | b39964
// agent: composer-2.5 | 2026-07-30 | lockstep gate diag | 626c3b
// agent: composer-2.5 | 2026-07-30 | solo lockstep always go | 4950ad
// agent: composer-2.5 | 2026-07-30 | lockstep slot tick tags | bd99f2
// agent: composer-2.5 | 2026-07-30 | lockstep syncing gate APIs | ae43d3
// agent: composer-2.5 | 2026-07-30 | lockstep join fixes logging | 5d65f6
// agent: composer-2.5 | 2026-07-30 | lockstep playout setter | 9b7f42
// agent: composer-2.5 | 2026-07-30 | capture buttons into lock slots | 60a1ec
// agent: composer-2.5 | 2026-07-30 | bits_for and step tick APIs | a119d0
// agent: composer-2.5 | 2026-07-30 | ack trims send window | 0bb3f0
// agent: composer-2.5 | 2026-07-30 | slot conflict triggers desync | b2989d
// agent: composer-2.5 | 2026-07-30 | remove peer on disconnect | 98511c
// agent: composer-2.5 | 2026-07-30 | hash mismatch stalls sync | abbc70
// agent: composer-2.5 | 2026-07-30 | gate capture next sim tick | ba4520
// agent: composer-2.5 | 2026-07-30 | peer count accessor | ea5827
