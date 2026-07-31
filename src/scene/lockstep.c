// agent: composer-2.5 | 2026-07-29 | lockstep clock module | 4a4ad4
// agent: composer-2.5 | 2026-07-30 | lockstep slot tick tags | bd99f2
// agent: composer-2.5 | 2026-07-30 | lockstep syncing gate APIs | ae43d3
// agent: composer-2.5 | 2026-07-30 | capture buttons into lock slots | 60a1ec
// agent: composer-2.5 | 2026-07-30 | bits_for and step tick APIs | a119d0
// agent: composer-2.5 | 2026-07-30 | ack trims send window | 0bb3f0
// agent: composer-2.5 | 2026-07-30 | slot conflict triggers desync | b2989d
// agent: composer-2.5 | 2026-07-30 | remove peer on disconnect | 98511c
// agent: composer-2.5 | 2026-07-30 | hash mismatch stalls sync | abbc70
// agent: composer-2.5 | 2026-07-30 | server clock owner gate | 16b04a
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
  bool got_ack;   /* remote has sent at least one LOCK_ACK */
  bool got_input; /* remote has sent at least one LOCK_INPUT (scene live) */
  uint32_t peer_id;
  NgLockSlot slots[NG_LOCK_RING];
  uint32_t highest_recv;
  uint32_t ack_our; /* remote reports they have world inputs through this tick */
} NgLockPeer;

typedef struct NgLockCtx {
  bool active;
  bool clock_owner; /* dedicated server / authoritative host owns sim clock */
  bool roster_ok;   /* mirror received host roster RESUME at least once */
  bool sim_started;
  bool syncing;
  bool await_phys;
  uint32_t resume_barrier; /* after end_sync: drop remote tip until sim passes this */
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
  // agent: cursor-grok-4.5 | 2026-07-31 | remove dead synth path | f2b236
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
  if (tick == 0 || g_lock.local_peer_id == 0) {
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
    /* Already committed for this tick — do not change bits (determinism). */
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

// agent: composer-2.5 | 2026-07-30 | wait all_have before zero-fill | 24fa25
static bool mod_lockstep_all_have(uint32_t tick) {
  if (tick == 0) {
    return true;
  }
  int checked = 0;
  for (int i = 0; i < g_lock.peer_count; i++) {
    NgLockPeer *p = &g_lock.peers[i];
    if (!p->alive) {
      continue;
    }
    checked++;
    if (!mod_lockstep_slot_has(p, tick)) {
      return false;
    }
  }
  if (checked == 0) {
    /* Solo player process: require local slot. Clock owner with no remotes: ok. */
    if (g_lock.local_peer_id == 0) {
      return true;
    }
    NgLockPeer *self = mod_lockstep_find_peer(g_lock.local_peer_id);
    return self && mod_lockstep_slot_has(self, tick);
  }
  return true;
}

bool mod_lockstep_all_peers_live(void) {
  // agent: composer-2.5 | 2026-07-30 | peer got_input longer grace | da9ef2
  if (g_lock.peer_count == 0) {
    return true;
  }
  int live = 0;
  for (int i = 0; i < g_lock.peer_count; i++) {
    NgLockPeer *p = &g_lock.peers[i];
    if (!p->alive) {
      continue;
    }
    live++;
    if (!p->got_input) {
      return false;
    }
  }
  return live > 0;
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

void mod_lockstep_set_clock_owner(bool owner) {
  // agent: composer-2.5 | 2026-07-30 | server clock owner gate | 16b04a
  g_lock.clock_owner = owner;
  if (owner) {
    g_lock.roster_ok = true;
  }
}

bool mod_lockstep_is_clock_owner(void) { return g_lock.clock_owner; }

void mod_lockstep_note_roster(void) {
  // agent: composer-2.5 | 2026-07-30 | server clock owner gate | 16b04a
  g_lock.roster_ok = true;
}

bool mod_lockstep_roster_ok(void) { return g_lock.roster_ok || g_lock.clock_owner; }

void mod_lockstep_set_local_peer(uint32_t peer_id) {
  // agent: composer-2.5 | 2026-07-30 | server clock owner gate | 16b04a
  /* peer_id 0 = clock owner with no local player slot (dedicated server). */
  g_lock.local_peer_id = peer_id;
  if (peer_id != 0) {
    mod_lockstep_add_peer(g_lock.local_peer_id);
  }
}

uint32_t mod_lockstep_local_peer_id(void) { return g_lock.local_peer_id; }

void mod_lockstep_clear_peers(void) {
  memset(g_lock.peers, 0, sizeof(g_lock.peers));
  g_lock.peer_count = 0;
  g_lock.local_ack = 0;
}

void mod_lockstep_add_peer(uint32_t peer_id) {
  // agent: composer-2.5 | 2026-07-30 | server clock owner gate | 16b04a
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
  /* Align ack watermarks to current clock — no history zero-fill (Gaffer: only
   * frame n+ needs input n+; late join resets rings in end_sync). */
  // agent: cursor-grok-4.5 | 2026-07-31 | gaffer reduce gate end_sync | 0f5fb7
  p->highest_recv = g_lock.sim_tick;
  p->ack_our = g_lock.sim_tick;
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

void mod_lockstep_apply_roster(const uint8_t *peer_ids, int count) {
  // agent: cursor-grok-4.5 | 2026-07-31 | apply authoritative resume roster | 6eea5d
  if (!peer_ids || count < 0) {
    return;
  }
  if (count > NG_LOCK_PEER_MAX) {
    count = NG_LOCK_PEER_MAX;
  }
  /* Drop anyone not on the host roster — otherwise all_have waits forever. */
  for (int i = g_lock.peer_count - 1; i >= 0; i--) {
    NgLockPeer *p = &g_lock.peers[i];
    if (!p->alive) {
      continue;
    }
    bool keep = false;
    for (int j = 0; j < count; j++) {
      if (peer_ids[j] != 0 && peer_ids[j] == (uint8_t)p->peer_id) {
        keep = true;
        break;
      }
    }
    if (!keep) {
      NG_LOG_INFO("lockstep: roster drop peer=%u", p->peer_id);
      mod_lockstep_remove_peer(p->peer_id);
    }
  }
  for (int j = 0; j < count; j++) {
    if (peer_ids[j] != 0) {
      mod_lockstep_add_peer(peer_ids[j]);
    }
  }
  mod_lockstep_note_roster();
}

bool mod_lockstep_needs_join_sync(void) {
  /* Mid-sim join once the lockstep clock has advanced at least one step. */
  return g_lock.active && g_lock.sim_tick > 0;
}

void mod_lockstep_begin_sync(uint32_t tick) {
  // agent: cursor-grok-4.5 | 2026-07-31 | gaffer reduce gate end_sync | 0f5fb7
  g_lock.syncing = true;
  if (tick > 0) {
    g_lock.sim_tick = tick;
  }
  /* Stop advertising a send tip past the pause tick (gate also skips tip growth). */
  g_lock.local_send_tick = g_lock.sim_tick;
  for (int i = 0; i < g_lock.peer_count; i++) {
    NgLockPeer *p = &g_lock.peers[i];
    if (!p->alive) {
      continue;
    }
    for (int s = 0; s < NG_LOCK_RING; s++) {
      NgLockSlot *sl = &p->slots[s];
      if (sl->present && sl->tick > g_lock.sim_tick) {
        sl->present = false;
        sl->bits = 0;
        sl->tick = 0;
      }
    }
  }
}

void mod_lockstep_end_sync(void) {
  // agent: cursor-grok-4.5 | 2026-07-31 | gaffer reduce gate end_sync | 0f5fb7
  /* Everyone resumes together at sim_tick: wipe rings, sample fresh from T+1. */
  g_lock.syncing = false;
  g_lock.await_phys = false;
  g_lock.sim_started = true;
  g_lock.local_send_tick = g_lock.sim_tick;
  /* Drop in-flight pre-PAUSE tip packets until we have stepped past that window. */
  g_lock.resume_barrier = g_lock.sim_tick + mod_lockstep_playout_ticks();
  for (int i = 0; i < g_lock.peer_count; i++) {
    if (!g_lock.peers[i].alive) {
      continue;
    }
    g_lock.peers[i].highest_recv = g_lock.sim_tick;
    g_lock.peers[i].ack_our = g_lock.sim_tick;
    memset(g_lock.peers[i].slots, 0, sizeof(g_lock.peers[i].slots));
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
  // agent: cursor-grok-4.5 | 2026-07-31 | gaffer reduce gate end_sync | 0f5fb7
  /* Gaffer Deterministic Lockstep gate (reduced):
   * sample local → grow tip to playout → wait all_have(n) → step. No invent,
   * no solo debounce, no RING/2 tip race, no post-join clock hold. */
  if (!g_lock.active) {
    return NG_LOCK_GATE_GO;
  }
  if (g_lock.local_peer_id == 0 && !g_lock.clock_owner) {
    mod_lockstep_set_local_peer(1);
  }

  const uint32_t next_sim = g_lock.sim_tick + 1u;
  const uint32_t playout = mod_lockstep_playout_ticks();

  /* Sample next_sim even while paused so RESUME has local input. Do NOT grow
   * the playout tip during PAUSE — those futures are sent on the wire and let
   * a joiner all_have/race ahead; end_sync then wipes mirrors → deadlock. */
  mod_lockstep_gen_local(next_sim);

  if (g_lock.syncing || g_lock.await_phys) {
    return NG_LOCK_GATE_STALL;
  }

  /* Grow playout tip only when live — not under resume_barrier (STALL + tip
   * growth re-feeds the joiner the +playout race). */
  if (g_lock.resume_barrier == 0u && g_lock.local_peer_id != 0) {
    const uint32_t send_target = g_lock.sim_tick + playout;
    if (g_lock.local_send_tick < send_target) {
      mod_lockstep_gen_local(g_lock.local_send_tick + 1u);
    }
  }

  if (g_lock.clock_owner) {
    /* Empty roster after scene restart: do not solo-race ahead of mirrors. */
    if (g_lock.peer_count == 0) {
      return NG_LOCK_GATE_BUFFER;
    }
    g_lock.sim_started = true;
    /* Host relays LOCK_INPUT; no synth rebroadcast (that doubled UDP + stalled). */
    // agent: cursor-grok-4.5 | 2026-07-31 | drop owner go rebroadcast | b2dafa
    if (!mod_lockstep_all_have(next_sim)) {
      return NG_LOCK_GATE_STALL;
    }
    return NG_LOCK_GATE_GO;
  }

  if (!g_lock.roster_ok) {
    return NG_LOCK_GATE_BUFFER;
  }
  if (g_lock.peer_count <= 1) {
    g_lock.sim_started = true;
    return NG_LOCK_GATE_GO;
  }
  if (!g_lock.sim_started) {
    if (g_lock.local_send_tick < g_lock.sim_tick + playout) {
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
  if (g_lock.resume_barrier > 0u && g_lock.sim_tick >= g_lock.resume_barrier) {
    g_lock.resume_barrier = 0u;
  }
  if (g_lock.sim_tick > 0 && (g_lock.sim_tick % 60u) == 0u) {
    g_lock.last_hash = hash;
    g_lock.last_hash_tick = g_lock.sim_tick;
  }
}

void mod_lockstep_store_remote_input(uint32_t peer_id, uint32_t tick, uint8_t bits) {
  // agent: composer-2.5 | 2026-07-30 | solo lockstep always go | 4950ad
  // agent: composer-2.5 | 2026-07-30 | slot conflict triggers desync | b2989d
  // agent: composer-2.5 | 2026-07-30 | server clock owner gate | 16b04a
  if (tick == 0 || peer_id == 0) {
    return;
  }
  /* During PAUSE/await or post-RESUME barrier: only next tick from remotes
   * (in-flight pre-PAUSE playout tips would let one peer race +playout). */
  if ((g_lock.syncing || g_lock.await_phys || g_lock.resume_barrier > 0u) &&
      tick > g_lock.sim_tick + 1u) {
    return;
  }
  NgLockPeer *p = mod_lockstep_find_peer(peer_id);
  if (!p) {
    /* Learn peers from the wire (cold multi-client / host synth). */
    mod_lockstep_add_peer(peer_id);
    p = mod_lockstep_find_peer(peer_id);
  }
  if (!p) {
    return;
  }
  p->got_input = true;
  NgLockSlot *s = &p->slots[tick % NG_LOCK_RING];
  if (s->present && s->tick == tick) {
    if (s->bits != bits) {
      if (g_lock.clock_owner) {
        /* Adopt newer wire bits before step; ignore conflicts after step. */
        if (tick > g_lock.sim_tick) {
          s->bits = bits;
          return;
        }
        NG_LOG_WARN("lockstep: ignore late input peer=%u tick=%u got=%u have=%u", peer_id, tick,
                    (unsigned)bits, (unsigned)s->bits);
        return;
      }
      /* Mirror: host/peer wire is commit authority — always adopt before step. */
      // agent: composer-2.5 | 2026-07-30 | host commit overwrites local | 2da677
      if (tick > g_lock.sim_tick) {
        s->bits = bits;
        return;
      }
      NG_LOG_WARN("lockstep: adopt remote bits peer=%u tick=%u was=%u now=%u", peer_id, tick,
                  (unsigned)s->bits, (unsigned)bits);
      s->bits = bits;
      return;
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
  p->got_ack = true;
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

// agent: composer-2.5 | 2026-07-30 | wait all_have before zero-fill | 24fa25
uint8_t mod_lockstep_bits_or(uint32_t tick) {
  uint8_t bits = 0;
  if (tick == 0) {
    return 0;
  }
  for (int i = 0; i < g_lock.peer_count; i++) {
    NgLockPeer *p = &g_lock.peers[i];
    if (!p->alive || !mod_lockstep_slot_has(p, tick)) {
      continue;
    }
    bits |= p->slots[tick % NG_LOCK_RING].bits;
  }
  return bits;
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
  // agent: cursor-grok-4.5 | 2026-07-31 | gaffer reduce gate end_sync | 0f5fb7
  if (!out_base_tick || !out_bits || max_count <= 0 || g_lock.local_send_tick == 0) {
    return 0;
  }
  NgLockPeer *self = mod_lockstep_find_peer(g_lock.local_peer_id);
  if (!self) {
    return 0;
  }
  /* Gaffer: redundant window ending at newest input. Must include the tip —
   * starting at tick 1 with NG_LOCK_INPUT_MAX(32) never reaches send>32 →
   * permanent STALL at tick 32. After end_sync wipe, skip leading holes or
   * the first missing slot aborts the whole window (joiner sends nothing). */
  uint32_t start =
      g_lock.local_send_tick > (uint32_t)max_count
          ? (g_lock.local_send_tick - (uint32_t)max_count + 1u)
          : 1u;
  /* Skip leading holes (end_sync wipe) so we still emit the live tip. */
  while (start <= g_lock.local_send_tick && !mod_lockstep_slot_has(self, start)) {
    start++;
  }
  if (start > g_lock.local_send_tick) {
    return 0;
  }
  *out_base_tick = start;
  int n = 0;
  for (uint32_t t = start; t <= g_lock.local_send_tick && n < max_count; t++) {
    if (!mod_lockstep_slot_has(self, t)) {
      break;
    }
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
// agent: composer-2.5 | 2026-07-30 | server clock owner gate | 16b04a
// agent: composer-2.5 | 2026-07-30 | wait all_have before zero-fill | 24fa25
// agent: composer-2.5 | 2026-07-30 | owner broadcast commit each step | f0bb9b
// agent: composer-2.5 | 2026-07-30 | send ahead while sim stalled | e20a8c
// agent: composer-2.5 | 2026-07-30 | owner wait then zero-fill | 81642f
// agent: composer-2.5 | 2026-07-30 | mirror stall until all_have | 98fe6a
// agent: composer-2.5 | 2026-07-30 | host commit overwrites local | 2da677
// agent: composer-2.5 | 2026-07-31 | gaffer gate playout stall | 48ee28
// agent: composer-2.5 | 2026-07-31 | gaffer tip sample sendahead | 2e5e49
// agent: cursor-grok-4.5 | 2026-07-31 | gaffer reduce gate end_sync | 0f5fb7
// agent: cursor-grok-4.5 | 2026-07-31 | drop owner go rebroadcast | b2dafa
// agent: cursor-grok-4.5 | 2026-07-31 | remove dead synth path | f2b236
// agent: cursor-grok-4.5 | 2026-07-31 | apply authoritative resume roster | 6eea5d
