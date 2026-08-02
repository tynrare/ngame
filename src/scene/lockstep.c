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
#include "engine/ng_proto.h"
#include "physics.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
// agent: composer-2.5 | 2026-07-31 | lockstep stats and adapt | 68202b

// agent: cursor-grok-4.5 | 2026-07-31 | host prune silent stall peers | a55e1c
static double mod_lockstep_wall_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

typedef struct NgLockSlot {
  bool present;
  bool predicted; /* last-input hold; confirm may correct */
  uint8_t bits;
  uint32_t tick; /* absolute tick; required so ring wrap cannot false-match */
  // agent: composer-2.5 | 2026-08-01 | slot action propose APIs | a8876f
  bool has_action;
  uint16_t action_id;
  uint8_t action_argc;
  float action_argv[NG_LOCK_ACTION_FLOATS];
} NgLockSlot;

typedef struct NgLockPeer {
  bool alive;
  bool got_ack;   /* remote has sent at least one LOCK_ACK */
  bool got_input; /* remote has sent at least one LOCK_INPUT (scene live) */
  uint32_t peer_id;
  NgLockSlot slots[NG_LOCK_RING];
  uint32_t highest_recv;
  uint32_t ack_our; /* remote reports they have world inputs through this tick */
  double last_input_wall; /* monotonic; host prunes silent peers (left) */
  uint8_t last_bits; /* prediction hold */
  // agent: composer-2.5 | 2026-08-01 | per-peer playout lockstep | ce507f
  uint8_t playout_ticks; /* 0 = use global session playout */
  uint8_t zf_window;
  uint8_t ok_window;
  bool ghost;
  double ghost_until;
  char ghost_name[32];
} NgLockPeer;

typedef struct NgLockCtx {
  bool active;
  // agent: composer-2.5 | 2026-08-01 | hybrid flag lockstep impl | 384379
  bool hybrid; /* false = classic Gaffer wait-for-all */
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
  uint32_t confirmed_tick; /* highest host-confirmed tick */
  double confirm_wait_start; /* wall when waiting on next confirm */
  NgLockConfirmPkt last_confirm;
  bool last_confirm_valid;
  NgLockConfirmPkt confirm_hist[NG_LOCK_CONFIRM_HIST];
  int confirm_hist_n;
  uint32_t confirm_bcast_tick; /* highest confirm marked sent on wire */
  uint32_t resim_to; /* >0: rollback target tip to resim through */
  NgLockPeer peers[NG_LOCK_PEER_MAX];
  int peer_count;
  uint32_t last_hash;
  uint32_t last_hash_tick;
  uint32_t local_ack;
  // agent: composer-2.5 | 2026-07-31 | lockstep stats and adapt | 68202b
  uint32_t stat_buffer;
  uint32_t stat_zero_fill;
  uint32_t stat_resim;
  uint32_t adapt_confirms;
  uint32_t adapt_zf;
  uint32_t adapt_buf_at_window;
  double adapt_last_wall;
  int adapt_env; /* -1 unset, 0 off, 1 on (default) */
  bool playout_dirty; /* SESSION should refresh peer playout */
  // agent: cursor-grok-4.5 | 2026-07-31 | remove dead synth path | f2b236
  // agent: composer-2.5 | 2026-07-31 | host confirm zero fill gate | 0e6e01
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
    self->last_input_wall = mod_lockstep_wall_now();
    return;
  }
  s->present = true;
  s->bits = bits;
  s->tick = tick;
  s->predicted = false;
  /* Fresh gen does not invent actions; propose_local_action may attach later. */
  s->has_action = false;
  s->action_id = 0;
  s->action_argc = 0;
  if (tick > g_lock.local_send_tick) {
    g_lock.local_send_tick = tick;
  }
  self->last_bits = bits;
  self->last_input_wall = mod_lockstep_wall_now();
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
    if (!p->alive || p->ghost) {
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
  g_lock.adapt_env = -1;
  mod_scene_physics_save_ring_clear();
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

// agent: composer-2.5 | 2026-08-01 | predict allow gate save | d6bd83
uint32_t mod_lockstep_predict_allow(void) {
  // agent: composer-2.5 | 2026-08-01 | hybrid flag lockstep impl | 384379
  if (!g_lock.hybrid) {
    return 0u; /* pure lockstep: no speculation */
  }
  /* SnapNet: prefer playout; shrink speculation as playout rises above default 6. */
  const uint32_t playout = mod_lockstep_playout_ticks();
  const uint32_t def = (uint32_t)NG_LOCK_PLAYOUT_TICKS;
  const uint32_t maxp = (uint32_t)NG_LOCK_PREDICT_MAX;
  const uint32_t excess = (playout > def) ? (playout - def) : 0u;
  uint32_t allow = (excess < maxp) ? (maxp - excess) : 0u;
  if (allow < 4u) {
    allow = 4u;
  }
  if (allow > maxp) {
    allow = maxp;
  }
  return allow;
}

// agent: composer-2.5 | 2026-07-31 | lockstep stats and adapt | 68202b
static bool mod_lockstep_adapt_enabled(void) {
  if (g_lock.adapt_env < 0) {
    const char *e = getenv("NG_LOCK_ADAPT_PLAYOUT");
    /* Default on; set NG_LOCK_ADAPT_PLAYOUT=0 to disable. */
    g_lock.adapt_env = (e && e[0] == '0' && e[1] == '\0') ? 0 : 1;
  }
  return g_lock.adapt_env != 0;
}

static void mod_lockstep_adapt_playout(bool had_zf) {
  // agent: composer-2.5 | 2026-08-01 | hybrid flag lockstep impl | 384379
  if (!g_lock.hybrid || !g_lock.clock_owner || !mod_lockstep_adapt_enabled()) {
    return;
  }
  g_lock.adapt_confirms++;
  if (had_zf) {
    g_lock.adapt_zf++;
  }
  const double now = mod_lockstep_wall_now();
  if (g_lock.adapt_last_wall <= 0.0) {
    g_lock.adapt_last_wall = now;
    g_lock.adapt_buf_at_window = g_lock.stat_buffer;
    return;
  }
  if ((now - g_lock.adapt_last_wall) < 1.0 && g_lock.adapt_confirms < 60u) {
    return;
  }
  /* Global floor stays near default; per-peer playout absorbs high-ping peers. */
  // agent: composer-2.5 | 2026-08-01 | per-peer playout lockstep | ce507f
  const uint32_t buf_delta = g_lock.stat_buffer - g_lock.adapt_buf_at_window;
  uint32_t playout = mod_lockstep_playout_ticks();
  if (g_lock.adapt_zf >= 8u && buf_delta >= 60u) {
    if (playout < 12u) {
      playout++;
    }
  } else if (g_lock.adapt_zf == 0u && buf_delta < 30u) {
    if (playout > (uint32_t)NG_LOCK_PLAYOUT_TICKS) {
      playout--;
    } else if (playout < (uint32_t)NG_LOCK_PLAYOUT_TICKS) {
      playout++;
    }
  }
  if (playout < 4u) {
    playout = 4u;
  }
  if (playout > 12u) {
    playout = 12u;
  }
  if (playout != mod_lockstep_playout_ticks()) {
    mod_lockstep_set_playout_ticks(playout);
    g_lock.playout_dirty = true;
  }
  /* Per-peer nudge from recent confirm miss windows. */
  for (int i = 0; i < g_lock.peer_count; i++) {
    NgLockPeer *p = &g_lock.peers[i];
    if (!p->alive || p->peer_id == 0 || p->ghost) {
      continue;
    }
    uint32_t pp = p->playout_ticks ? p->playout_ticks : (uint32_t)NG_LOCK_PLAYOUT_TICKS;
    if (p->zf_window >= 3u) {
      if (pp < 12u) {
        pp++;
      }
    } else if (p->zf_window == 0u && p->ok_window >= 30u) {
      if (pp > (uint32_t)NG_LOCK_PLAYOUT_TICKS) {
        pp--;
      }
    }
    if (pp < 4u) {
      pp = 4u;
    }
    if (pp > 12u) {
      pp = 12u;
    }
    if (pp != (p->playout_ticks ? p->playout_ticks : (uint32_t)NG_LOCK_PLAYOUT_TICKS) ||
        (p->playout_ticks == 0 && pp != (uint32_t)NG_LOCK_PLAYOUT_TICKS)) {
      p->playout_ticks = (uint8_t)pp;
      g_lock.playout_dirty = true;
    }
    p->zf_window = 0;
    p->ok_window = 0;
  }
  g_lock.adapt_confirms = 0;
  g_lock.adapt_zf = 0;
  g_lock.adapt_last_wall = now;
  g_lock.adapt_buf_at_window = g_lock.stat_buffer;
}

static NgLockGate mod_lockstep_gate_buf(void) {
  g_lock.stat_buffer++;
  return NG_LOCK_GATE_BUFFER;
}

void mod_lockstep_stats(uint32_t *out_playout, uint32_t *out_buf, uint32_t *out_zf,
                        uint32_t *out_resim) {
  if (out_playout) {
    *out_playout = mod_lockstep_playout_ticks();
  }
  if (out_buf) {
    *out_buf = g_lock.stat_buffer;
  }
  if (out_zf) {
    *out_zf = g_lock.stat_zero_fill;
  }
  if (out_resim) {
    *out_resim = g_lock.stat_resim;
  }
}

// agent: composer-2.5 | 2026-08-01 | per-peer playout lockstep | ce507f
uint32_t mod_lockstep_peer_playout(uint32_t peer_id) {
  NgLockPeer *p = mod_lockstep_find_peer(peer_id);
  if (p && p->playout_ticks != 0) {
    return p->playout_ticks;
  }
  return mod_lockstep_playout_ticks();
}

void mod_lockstep_set_peer_playout(uint32_t peer_id, uint32_t ticks) {
  NgLockPeer *p = mod_lockstep_find_peer(peer_id);
  if (!p) {
    return;
  }
  if (ticks < 4u) {
    ticks = 4u;
  }
  if (ticks > 12u) {
    ticks = 12u;
  }
  if (p->playout_ticks != (uint8_t)ticks) {
    p->playout_ticks = (uint8_t)ticks;
    g_lock.playout_dirty = true;
  }
}

bool mod_lockstep_take_playout_dirty(void) {
  if (!g_lock.playout_dirty) {
    return false;
  }
  g_lock.playout_dirty = false;
  return true;
}

void mod_lockstep_mark_ghost(uint32_t peer_id, const char *name, double until_wall) {
  NgLockPeer *p = mod_lockstep_find_peer(peer_id);
  if (!p || !name || name[0] == '\0') {
    return;
  }
  p->ghost = true;
  p->ghost_until = until_wall;
  strncpy(p->ghost_name, name, sizeof(p->ghost_name) - 1);
  p->ghost_name[sizeof(p->ghost_name) - 1] = '\0';
  p->got_input = false;
  NG_LOG_WARN("lockstep: ghost peer=%u name=%s until=%.1fs", peer_id, p->ghost_name,
              until_wall - mod_lockstep_wall_now());
}

bool mod_lockstep_take_ghost_by_name(const char *name, uint32_t *out_peer_id) {
  if (!name || name[0] == '\0') {
    return false;
  }
  for (int i = 0; i < g_lock.peer_count; i++) {
    NgLockPeer *p = &g_lock.peers[i];
    if (!p->alive || !p->ghost) {
      continue;
    }
    if (strncmp(p->ghost_name, name, sizeof(p->ghost_name)) != 0) {
      continue;
    }
    p->ghost = false;
    p->ghost_until = 0.0;
    p->ghost_name[0] = '\0';
    p->last_input_wall = mod_lockstep_wall_now();
    if (out_peer_id) {
      *out_peer_id = p->peer_id;
    }
    NG_LOG_INFO("lockstep: rebind ghost peer=%u name=%s", p->peer_id, name);
    return true;
  }
  return false;
}

bool mod_lockstep_peer_is_ghost(uint32_t peer_id) {
  NgLockPeer *p = mod_lockstep_find_peer(peer_id);
  return p && p->ghost;
}

int mod_lockstep_expire_ghosts(uint32_t *out_dropped, int max_dropped) {
  if (!g_lock.active || !g_lock.clock_owner || max_dropped <= 0) {
    return 0;
  }
  const double now = mod_lockstep_wall_now();
  int n = 0;
  for (int i = g_lock.peer_count - 1; i >= 0 && n < max_dropped; i--) {
    NgLockPeer *p = &g_lock.peers[i];
    if (!p->alive || !p->ghost) {
      continue;
    }
    if (now < p->ghost_until) {
      continue;
    }
    const uint32_t id = p->peer_id;
    NG_LOG_WARN("lockstep: ghost expired peer=%u — leave", id);
    mod_lockstep_remove_peer(id);
    if (out_dropped) {
      out_dropped[n] = id;
    }
    n++;
  }
  return n;
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

// agent: composer-2.5 | 2026-08-01 | hybrid flag lockstep impl | 384379
void mod_lockstep_set_hybrid(bool hybrid) { g_lock.hybrid = hybrid; }

bool mod_lockstep_is_hybrid(void) { return g_lock.hybrid; }

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
  p->last_input_wall = mod_lockstep_wall_now();
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
  g_lock.confirmed_tick = g_lock.sim_tick;
  g_lock.confirm_wait_start = 0.0;
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
  mod_scene_physics_save_ring_clear();
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

static void mod_lockstep_ensure_predict(uint32_t tick) {
  // agent: composer-2.5 | 2026-07-31 | predict fill gate rollback | 5c8d67
  if (tick == 0) {
    return;
  }
  for (int i = 0; i < g_lock.peer_count; i++) {
    NgLockPeer *p = &g_lock.peers[i];
    if (!p->alive || p->peer_id == 0) {
      continue;
    }
    if (mod_lockstep_slot_has(p, tick)) {
      continue;
    }
    /* Hold last input (GGPO/SnapNet). Do not bump highest_recv (not real wire). */
    NgLockSlot *s = &p->slots[tick % NG_LOCK_RING];
    s->present = true;
    s->predicted = true;
    s->bits = p->last_bits;
    s->tick = tick;
    /* Never hold-fire: predicted slots copy bits only. */
    s->has_action = false;
    s->action_id = 0;
    s->action_argc = 0;
  }
}

NgLockGate mod_lockstep_gate(void) {
  // agent: composer-2.5 | 2026-07-31 | host confirm zero fill gate | 0e6e01
  // agent: composer-2.5 | 2026-07-31 | predict fill gate rollback | 5c8d67
  /* Confirmed GO, or mirror predict up to PREDICT_MAX with last-input. */
  // agent: composer-2.5 | 2026-07-31 | drop mode b comments lock | 02a2b4
  if (!g_lock.active) {
    return NG_LOCK_GATE_GO;
  }
  if (g_lock.local_peer_id == 0 && !g_lock.clock_owner) {
    mod_lockstep_set_local_peer(1);
  }

  const uint32_t next_sim = g_lock.sim_tick + 1u;
  const uint32_t playout = mod_lockstep_playout_ticks();

  mod_lockstep_gen_local(next_sim);

  if (g_lock.syncing || g_lock.await_phys) {
    return NG_LOCK_GATE_STALL;
  }

  if (g_lock.resume_barrier == 0u && g_lock.local_peer_id != 0) {
    const uint32_t send_target = g_lock.sim_tick + playout;
    if (g_lock.local_send_tick < send_target) {
      mod_lockstep_gen_local(g_lock.local_send_tick + 1u);
    }
  }

  if (g_lock.clock_owner) {
    // agent: composer-2.5 | 2026-07-31 | confirm hist unsent bcast | 226e5b
    /* Confirm after gen_local (above). Net flush broadcasts every unsent hist
     * tick — do not rely on last_confirm alone across multi-step frames. */
    if (g_lock.peer_count == 0) {
      return mod_lockstep_gate_buf();
    }
    g_lock.sim_started = true;
    if (g_lock.confirmed_tick < next_sim) {
      NgLockConfirmPkt tmp;
      (void)mod_lockstep_host_try_confirm(&tmp);
    }
    if (g_lock.confirmed_tick >= next_sim) {
      return NG_LOCK_GATE_GO;
    }
    return NG_LOCK_GATE_STALL;
  }

  if (!g_lock.roster_ok) {
    return mod_lockstep_gate_buf();
  }
  if (g_lock.peer_count <= 1) {
    /* Mirror with only self still needs host LOCK_CONFIRM — never self-confirm
     * (that races ahead of the server and desyncs). */
    // agent: composer-2.5 | 2026-07-31 | mirrors never self confirm | 6b113e
    g_lock.sim_started = true;
    if (g_lock.confirmed_tick >= next_sim) {
      return NG_LOCK_GATE_GO;
    }
    return NG_LOCK_GATE_STALL;
  }
  if (!g_lock.sim_started) {
    if (g_lock.local_send_tick < g_lock.sim_tick + playout) {
      return mod_lockstep_gate_buf();
    }
    g_lock.sim_started = true;
  }
  if (g_lock.confirmed_tick >= next_sim) {
    return NG_LOCK_GATE_GO;
  }
  /* Mirror prediction: step ahead of confirm with last remote input. */
  // agent: composer-2.5 | 2026-08-01 | predict allow gate save | d6bd83
  // agent: composer-2.5 | 2026-08-01 | hybrid flag lockstep impl | 384379
  if (g_lock.hybrid) {
    NgLockPeer *self = mod_lockstep_find_peer(g_lock.local_peer_id);
    const uint32_t pred_allow = mod_lockstep_predict_allow();
    if (g_lock.confirmed_tick > 0 && next_sim <= g_lock.confirmed_tick + pred_allow && self &&
        mod_lockstep_slot_has(self, next_sim)) {
      mod_lockstep_ensure_predict(next_sim);
      return NG_LOCK_GATE_GO;
    }
  }
  return NG_LOCK_GATE_STALL;
}

void mod_lockstep_on_stepped(uint32_t tick, uint32_t hash) {
  // agent: composer-2.5 | 2026-07-30 | lockstep own sim clock | b39964
  // agent: composer-2.5 | 2026-08-01 | hash only confirmed tips | 2c31a3
  (void)tick;
  g_lock.sim_tick += 1u;
  g_lock.step_tick = 0;
  if (g_lock.resume_barrier > 0u && g_lock.sim_tick >= g_lock.resume_barrier) {
    g_lock.resume_barrier = 0u;
  }
  /* Publish hash only for confirmed tips — hybrid predict ahead would
   * compare speculative worlds to the clock owner's authoritative hash. */
  if (g_lock.sim_tick > 0 && (g_lock.sim_tick % 60u) == 0u &&
      g_lock.sim_tick <= g_lock.confirmed_tick) {
    g_lock.last_hash = hash;
    g_lock.last_hash_tick = g_lock.sim_tick;
  }
  /* Save confirmed and predicted tips so confirm mismatch can Restore@(c-1).
   * Host rarely predicts (steps on confirm); mirrors predict ≤ predict_allow. */
  // agent: composer-2.5 | 2026-07-31 | predict save ring depth | 752810
  // agent: composer-2.5 | 2026-08-01 | predict allow gate save | d6bd83
  // agent: composer-2.5 | 2026-08-01 | hybrid flag lockstep impl | 384379
  if (g_lock.hybrid) {
    const uint32_t pred_allow = mod_lockstep_predict_allow();
    if (g_lock.confirmed_tick != 0 && g_lock.sim_tick >= g_lock.confirmed_tick &&
        g_lock.sim_tick <= g_lock.confirmed_tick + pred_allow) {
      mod_scene_physics_save_ring_push(g_lock.sim_tick);
    } else if (g_lock.sim_tick == g_lock.confirmed_tick) {
      mod_scene_physics_save_ring_push(g_lock.sim_tick);
    }
  }
}

void mod_lockstep_store_remote_input(uint32_t peer_id, uint32_t tick, uint8_t bits,
                                     const NgLockAction *action) {
  // agent: composer-2.5 | 2026-07-30 | solo lockstep always go | 4950ad
  // agent: composer-2.5 | 2026-07-30 | slot conflict triggers desync | b2989d
  // agent: composer-2.5 | 2026-07-30 | server clock owner gate | 16b04a
  // agent: composer-2.5 | 2026-07-31 | host confirm zero fill gate | 0e6e01
  // agent: composer-2.5 | 2026-07-31 | late input still heartbeat | 37caa0
  // agent: composer-2.5 | 2026-08-01 | slot action propose APIs | a8876f
  if (tick == 0 || peer_id == 0) {
    return;
  }
  NgLockPeer *p = mod_lockstep_find_peer(peer_id);
  if (!p) {
    /* Roster/connect/READY only — wire-learn re-added pruned leavers and
     * asymmetric all_have STALLs (Gaffer: host roster is authority). */
    // agent: cursor-grok-4.5 | 2026-07-31 | no wire learn any side | b7c0ff
    return;
  }
  /* Always heartbeat — late packets still prove the peer is alive
   * (confirm may have already zero-filled this tick; do not silent-prune). */
  p->got_input = true;
  p->last_input_wall = mod_lockstep_wall_now();
  p->last_bits = bits;
  /* Drop late inputs after host confirm (bits already committed). */
  if (g_lock.confirmed_tick != 0 && tick <= g_lock.confirmed_tick) {
    return;
  }
  // agent: composer-2.5 | 2026-08-01 | per-peer playout lockstep | ce507f
  /* Anti-megapacket: ignore far-ahead tips after blackout (FFF-302). */
  if (g_lock.clock_owner && g_lock.confirmed_tick != 0) {
    const uint32_t max_ahead = g_lock.confirmed_tick + mod_lockstep_predict_allow() +
                               mod_lockstep_peer_playout(peer_id) + 2u;
    if (tick > max_ahead) {
      return;
    }
  }
  /* During PAUSE/await or post-RESUME barrier: only next tick from remotes
   * (in-flight pre-PAUSE playout tips would let one peer race +playout). */
  if ((g_lock.syncing || g_lock.await_phys || g_lock.resume_barrier > 0u) &&
      tick > g_lock.sim_tick + 1u) {
    return;
  }
  NgLockSlot *s = &p->slots[tick % NG_LOCK_RING];
  const bool act_present = action && action->present;
  // agent: composer-2.5 | 2026-08-01 | keep wire action on adopt | 0b6b21
  if (s->present && s->tick == tick) {
    /* Action-less redundant INPUT must not wipe a prior propose. Only upgrade
     * when the wire carries an action (or bits change). */
    if (s->bits != bits ||
        (act_present &&
         (!s->has_action || s->action_id != action->id || s->action_argc != action->argc))) {
      if (g_lock.clock_owner) {
        /* Adopt newer wire bits before step; ignore conflicts after step. */
        if (tick > g_lock.sim_tick) {
          s->bits = bits;
          s->predicted = false;
          if (act_present) {
            s->has_action = true;
            s->action_id = action->id;
            s->action_argc = action->argc;
            memcpy(s->action_argv, action->argv, sizeof(float) * action->argc);
          }
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
        s->predicted = false;
        if (act_present) {
          s->has_action = true;
          s->action_id = action->id;
          s->action_argc = action->argc;
          memcpy(s->action_argv, action->argv, sizeof(float) * action->argc);
        }
        return;
      }
      NG_LOG_WARN("lockstep: adopt remote bits peer=%u tick=%u was=%u now=%u", peer_id, tick,
                  (unsigned)s->bits, (unsigned)bits);
      s->bits = bits;
      s->predicted = false;
      if (act_present) {
        s->has_action = true;
        s->action_id = action->id;
        s->action_argc = action->argc;
        memcpy(s->action_argv, action->argv, sizeof(float) * action->argc);
      }
      return;
    }
    return;
  }
  s->present = true;
  s->bits = bits;
  s->tick = tick;
  s->predicted = false;
  if (act_present) {
    s->has_action = true;
    s->action_id = action->id;
    s->action_argc = action->argc <= NG_LOCK_ACTION_FLOATS ? action->argc : NG_LOCK_ACTION_FLOATS;
    memcpy(s->action_argv, action->argv, sizeof(float) * s->action_argc);
  } else {
    s->has_action = false;
    s->action_id = 0;
    s->action_argc = 0;
  }
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

int mod_lockstep_fill_send_window(uint32_t *out_base_tick, uint8_t *out_bits,
                                  NgLockAction *out_actions, int max_count) {
  // agent: cursor-grok-4.5 | 2026-07-31 | gaffer reduce gate end_sync | 0f5fb7
  // agent: composer-2.5 | 2026-08-01 | slot action propose APIs | a8876f
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
    const NgLockSlot *sl = &self->slots[t % NG_LOCK_RING];
    out_bits[n] = sl->bits;
    if (out_actions) {
      memset(&out_actions[n], 0, sizeof(out_actions[n]));
      if (sl->has_action) {
        out_actions[n].present = 1;
        out_actions[n].id = sl->action_id;
        out_actions[n].argc = sl->action_argc;
        memcpy(out_actions[n].argv, sl->action_argv, sizeof(float) * sl->action_argc);
      }
    }
    n++;
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

void mod_lockstep_on_net_lost(void) {
  // agent: cursor-grok-4.5 | 2026-07-31 | lockstep net lost buffer | 939686
  /* Mirror lost upstream: stop all_have-waiting on a dead roster (death spiral). */
  if (!g_lock.active || g_lock.clock_owner) {
    return;
  }
  g_lock.syncing = false;
  g_lock.await_phys = false;
  g_lock.resume_barrier = 0u;
  g_lock.roster_ok = false;
  g_lock.sim_started = false;
  const uint32_t self = g_lock.local_peer_id;
  mod_lockstep_clear_peers();
  if (self != 0u) {
    mod_lockstep_add_peer(self);
  }
  NG_LOG_INFO("lockstep: net lost — roster cleared peer=%u (wait RESUME)", self);
}

int mod_lockstep_prune_silent_peers(uint32_t *out_dropped, int max_dropped) {
  // agent: cursor-grok-4.5 | 2026-07-31 | host prune silent stall peers | a55e1c
  // agent: composer-2.5 | 2026-08-01 | per-peer playout lockstep | ce507f
  /* Hard leave after PRUNE_SEC (ghost seats expire separately). */
  if (!g_lock.active || !g_lock.clock_owner || max_dropped <= 0) {
    return 0;
  }
  const uint32_t next_sim = g_lock.sim_tick + 1u;
  if (next_sim == 0u) {
    return 0;
  }
  const double now = mod_lockstep_wall_now();
  const double grace = (double)NG_LOCK_PRUNE_SEC;
  int n = 0;
  n += mod_lockstep_expire_ghosts(out_dropped, max_dropped);
  for (int i = g_lock.peer_count - 1; i >= 0 && n < max_dropped; i--) {
    NgLockPeer *p = &g_lock.peers[i];
    if (!p->alive || p->peer_id == 0 || p->peer_id == g_lock.local_peer_id || p->ghost) {
      continue;
    }
    if (mod_lockstep_slot_has(p, next_sim)) {
      continue;
    }
    const double last = p->last_input_wall > 0.0 ? p->last_input_wall : now;
    if ((now - last) < grace) {
      continue;
    }
    const uint32_t id = p->peer_id;
    NG_LOG_WARN("lockstep: silent peer=%u (%.2fs) — treat as leave", id, now - last);
    mod_lockstep_remove_peer(id);
    if (out_dropped) {
      out_dropped[n] = id;
    }
    n++;
  }
  return n;
}

// agent: composer-2.5 | 2026-07-31 | host confirm zero fill gate | 0e6e01
uint32_t mod_lockstep_confirmed_tick(void) { return g_lock.confirmed_tick; }

// agent: composer-2.5 | 2026-07-31 | confirm hist unsent bcast | 226e5b
static void mod_lockstep_hist_push(const NgLockConfirmPkt *pkt) {
  if (!pkt || pkt->tick == 0) {
    return;
  }
  for (int i = 0; i < g_lock.confirm_hist_n; i++) {
    if (g_lock.confirm_hist[i].tick == pkt->tick) {
      g_lock.confirm_hist[i] = *pkt;
      return;
    }
  }
  if (g_lock.confirm_hist_n >= NG_LOCK_CONFIRM_HIST) {
    memmove(&g_lock.confirm_hist[0], &g_lock.confirm_hist[1],
            sizeof(g_lock.confirm_hist[0]) * (NG_LOCK_CONFIRM_HIST - 1));
    g_lock.confirm_hist_n = NG_LOCK_CONFIRM_HIST - 1;
  }
  g_lock.confirm_hist[g_lock.confirm_hist_n++] = *pkt;
}

void mod_lockstep_set_confirmed_tick(uint32_t tick) {
  g_lock.confirmed_tick = tick;
  g_lock.confirm_wait_start = 0.0;
  /* Catchup/import: treat as already delivered — no hist to rebroadcast. */
  if (tick >= g_lock.confirm_bcast_tick) {
    g_lock.confirm_bcast_tick = tick;
  }
}

bool mod_lockstep_apply_confirm(const NgLockConfirmPkt *pkt) {
  // agent: composer-2.5 | 2026-07-31 | predict fill gate rollback | 5c8d67
  if (!pkt || pkt->tick == 0 || pkt->peer_count > NG_LOCK_PEER_MAX) {
    return false;
  }
  if (pkt->tick <= g_lock.confirmed_tick) {
    return true;
  }
  const uint32_t expect = g_lock.confirmed_tick + 1u;
  if (g_lock.confirmed_tick != 0 && pkt->tick != expect) {
    return false; /* gap — needs PHYS catchup */
  }
  if (g_lock.confirmed_tick == 0 && g_lock.sim_tick > 0 && pkt->tick != g_lock.sim_tick + 1u &&
      pkt->tick != 1u) {
    return false;
  }

  bool mismatch = false;
  if (g_lock.sim_tick >= pkt->tick) {
    for (uint8_t i = 0; i < pkt->peer_count; i++) {
      NgLockPeer *p = mod_lockstep_find_peer(pkt->peer_ids[i]);
      if (!p || !mod_lockstep_slot_has(p, pkt->tick)) {
        mismatch = true;
        break;
      }
      if (p->slots[pkt->tick % NG_LOCK_RING].bits != pkt->bits[i]) {
        mismatch = true;
        break;
      }
      {
        const NgLockSlot *sl = &p->slots[pkt->tick % NG_LOCK_RING];
        const bool want = pkt->actions[i].present != 0;
        if (sl->has_action != want ||
            (want && (sl->action_id != pkt->actions[i].id ||
                      sl->action_argc != pkt->actions[i].argc))) {
          // agent: composer-2.5 | 2026-08-02 | confirm mismatch action detail | 688339
          NG_LOG_INFO("lockstep: confirm action mismatch tick=%u peer=%u had=%d want=%d "
                      "pred=%d (hybrid predict never carries actions)",
                      pkt->tick, pkt->peer_ids[i], sl->has_action ? 1 : 0, want ? 1 : 0,
                      sl->predicted ? 1 : 0);
          mismatch = true;
          break;
        }
      }
    }
  }

  for (uint8_t i = 0; i < pkt->peer_count; i++) {
    const uint32_t pid = pkt->peer_ids[i];
    if (pid == 0) {
      continue;
    }
    NgLockPeer *p = mod_lockstep_find_peer(pid);
    if (!p) {
      mod_lockstep_add_peer(pid);
      p = mod_lockstep_find_peer(pid);
    }
    if (!p) {
      continue;
    }
    NgLockSlot *s = &p->slots[pkt->tick % NG_LOCK_RING];
    s->present = true;
    s->predicted = false;
    s->bits = pkt->bits[i];
    s->tick = pkt->tick;
    if (pkt->actions[i].present) {
      s->has_action = true;
      s->action_id = pkt->actions[i].id;
      s->action_argc = pkt->actions[i].argc <= NG_LOCK_ACTION_FLOATS ? pkt->actions[i].argc
                                                                      : NG_LOCK_ACTION_FLOATS;
      memcpy(s->action_argv, pkt->actions[i].argv, sizeof(float) * s->action_argc);
      NG_LOG_INFO("lockstep: confirm adopt action tick=%u peer=%u id=%u", pkt->tick, pid,
                  (unsigned)s->action_id);
    } else {
      if (s->has_action) {
        NG_LOG_INFO("lockstep: confirm clear action tick=%u peer=%u", pkt->tick, pid);
      }
      s->has_action = false;
      s->action_id = 0;
      s->action_argc = 0;
    }
    p->last_bits = pkt->bits[i];
    p->got_input = true;
    mod_lockstep_advance_contiguous(p);
  }
  g_lock.confirmed_tick = pkt->tick;
  g_lock.confirm_wait_start = 0.0;

  if (mismatch && pkt->tick > 0) {
    const uint32_t tip = g_lock.sim_tick;
    const uint32_t restore = pkt->tick - 1u;
    if (restore > 0 && mod_scene_physics_save_ring_restore(restore)) {
      g_lock.sim_tick = restore;
      if (tip > restore) {
        // agent: composer-2.5 | 2026-07-31 | lockstep stats and adapt | 68202b
        g_lock.resim_to = tip;
        g_lock.stat_resim++;
      }
      NG_LOG_INFO("lockstep: rollback confirm=%u restore=%u resim_to=%u", pkt->tick, restore, tip);
    } else if (restore == 0) {
      /* No save at 0 — cannot rollback first tick; accept snap-forward. */
      NG_LOG_WARN("lockstep: confirm mismatch tick=%u but no save@0", pkt->tick);
    } else {
      // agent: composer-2.5 | 2026-07-31 | predict save ring depth | 752810
      NG_LOG_WARN("lockstep: confirm mismatch tick=%u missing save@%u — await PHYS", pkt->tick,
                  restore);
      /* Do not keep simulating a wrong world; host hash streak will PHYS. */
      if (!g_lock.clock_owner) {
        g_lock.await_phys = true;
      }
    }
  }
  return true;
}

void mod_lockstep_on_soft_phys(uint32_t tick) {
  // agent: composer-2.5 | 2026-07-31 | predict save ring depth | 752810
  if (tick == 0) {
    return;
  }
  g_lock.sim_tick = tick;
  g_lock.confirmed_tick = tick;
  g_lock.confirm_bcast_tick = tick;
  g_lock.confirm_wait_start = 0.0;
  g_lock.local_send_tick = tick;
  g_lock.resim_to = 0;
  g_lock.await_phys = false;
  for (int i = 0; i < g_lock.peer_count; i++) {
    NgLockPeer *p = &g_lock.peers[i];
    if (!p->alive) {
      continue;
    }
    p->highest_recv = tick;
    p->ack_our = tick;
    for (int s = 0; s < NG_LOCK_RING; s++) {
      NgLockSlot *sl = &p->slots[s];
      if (sl->present && sl->tick > tick) {
        sl->present = false;
        sl->bits = 0;
        sl->tick = 0;
        sl->predicted = false;
        sl->has_action = false;
        sl->action_id = 0;
        sl->action_argc = 0;
      }
    }
  }
  mod_scene_physics_save_ring_clear();
  if (tick > 0) {
    mod_scene_physics_save_ring_push(tick);
  }
}

uint32_t mod_lockstep_resim_to(void) { return g_lock.resim_to; }

void mod_lockstep_clear_resim(void) { g_lock.resim_to = 0; }

bool mod_lockstep_host_try_confirm(NgLockConfirmPkt *out) {
  if (!out || !g_lock.clock_owner || !g_lock.active) {
    return false;
  }
  if (g_lock.syncing || g_lock.await_phys || g_lock.peer_count == 0) {
    return false;
  }
  const uint32_t next = g_lock.confirmed_tick + 1u;
  if (next == 0u) {
    return false;
  }
  /* Do not confirm far ahead of sim — one tick at a time. */
  if (next > g_lock.sim_tick + 1u) {
    return false;
  }
  const bool ready = mod_lockstep_all_have(next);
  const double now = mod_lockstep_wall_now();
  if (!ready) {
    // agent: composer-2.5 | 2026-08-01 | hybrid flag lockstep impl | 384379
    if (!g_lock.hybrid) {
      /* Classic Gaffer: hitch until all_have — never zero-fill. */
      return false;
    }
    if (g_lock.confirm_wait_start <= 0.0) {
      g_lock.confirm_wait_start = now;
    }
    if ((now - g_lock.confirm_wait_start) < (double)NG_LOCK_CONFIRM_SEC) {
      return false;
    }
  }
  memset(out, 0, sizeof(*out));
  out->tick = next;
  uint8_t miss = 0;
  for (int i = 0; i < g_lock.peer_count && out->peer_count < NG_LOCK_PEER_MAX; i++) {
    NgLockPeer *p = &g_lock.peers[i];
    if (!p->alive || p->peer_id == 0) {
      continue;
    }
    const uint8_t idx = out->peer_count;
    out->peer_ids[idx] = (uint8_t)p->peer_id;
    if (mod_lockstep_slot_has(p, next)) {
      const NgLockSlot *sl = &p->slots[next % NG_LOCK_RING];
      out->bits[idx] = sl->bits;
      if (sl->has_action) {
        out->actions[idx].present = 1;
        out->actions[idx].id = sl->action_id;
        out->actions[idx].argc = sl->action_argc;
        memcpy(out->actions[idx].argv, sl->action_argv, sizeof(float) * sl->action_argc);
      }
    } else {
      out->bits[idx] = 0; /* deadline / disconnect fill */
      /* Zero-fill: no action. */
      miss |= (uint8_t)(1u << idx);
    }
    out->peer_count++;
  }
  out->miss_mask = miss;
  if (out->peer_count == 0) {
    return false;
  }
  {
    int nact = 0;
    for (uint8_t i = 0; i < out->peer_count; i++) {
      if (out->actions[i].present) {
        nact++;
      }
    }
    if (nact > 0) {
      // agent: composer-2.5 | 2026-08-02 | confirm mismatch action detail | 688339
      NG_LOG_INFO("lockstep: CONFIRM tick=%u peers=%u actions=%d miss=0x%02x", next,
                  (unsigned)out->peer_count, nact, (unsigned)miss);
    }
  }
  if (!mod_lockstep_apply_confirm(out)) {
    return false;
  }
  g_lock.last_confirm = *out;
  g_lock.last_confirm_valid = true;
  // agent: composer-2.5 | 2026-07-31 | confirm hist unsent bcast | 226e5b
  mod_lockstep_hist_push(out);
  // agent: composer-2.5 | 2026-07-31 | lockstep stats and adapt | 68202b
  // agent: composer-2.5 | 2026-08-01 | per-peer playout lockstep | ce507f
  for (uint8_t i = 0; i < out->peer_count; i++) {
    NgLockPeer *p = mod_lockstep_find_peer(out->peer_ids[i]);
    if (!p) {
      continue;
    }
    if (miss & (uint8_t)(1u << i)) {
      if (p->zf_window < 255u) {
        p->zf_window++;
      }
    } else if (p->ok_window < 255u) {
      p->ok_window++;
    }
  }
  if (miss != 0) {
    g_lock.stat_zero_fill++;
    NG_LOG_WARN("lockstep: CONFIRM tick=%u zero-fill mask=0x%02x", next, (unsigned)miss);
  }
  mod_lockstep_adapt_playout(miss != 0);
  return true;
}

bool mod_lockstep_copy_last_confirm(NgLockConfirmPkt *out) {
  if (!out || !g_lock.last_confirm_valid) {
    return false;
  }
  *out = g_lock.last_confirm;
  return true;
}

bool mod_lockstep_next_unsent_confirm(NgLockConfirmPkt *out) {
  // agent: composer-2.5 | 2026-07-31 | confirm hist unsent bcast | 226e5b
  if (!out || g_lock.confirmed_tick == 0) {
    return false;
  }
  const uint32_t want = g_lock.confirm_bcast_tick + 1u;
  if (want == 0u || want > g_lock.confirmed_tick) {
    return false;
  }
  for (int i = 0; i < g_lock.confirm_hist_n; i++) {
    if (g_lock.confirm_hist[i].tick == want) {
      *out = g_lock.confirm_hist[i];
      return true;
    }
  }
  return false;
}

void mod_lockstep_mark_confirm_sent(uint32_t tick) {
  // agent: composer-2.5 | 2026-07-31 | confirm hist unsent bcast | 226e5b
  if (tick == g_lock.confirm_bcast_tick + 1u) {
    g_lock.confirm_bcast_tick = tick;
  }
}

uint8_t mod_lockstep_last_bits(uint32_t peer_id) {
  NgLockPeer *p = mod_lockstep_find_peer(peer_id);
  return p ? p->last_bits : 0;
}

// agent: composer-2.5 | 2026-08-01 | slot action propose APIs | a8876f
bool mod_lockstep_propose_local_action(uint16_t action_id, uint8_t argc, const float *argv) {
  // agent: composer-2.5 | 2026-08-01 | propose future tip only | e6c793
  // agent: composer-2.5 | 2026-08-02 | refuse propose on confirmed tip | ea6352
  if (argc > NG_LOCK_ACTION_FLOATS || (argc > 0 && !argv)) {
    return false;
  }
  if (!g_lock.active || g_lock.local_peer_id == 0) {
    return false;
  }
  /* Prefer sendahead tip; never attach to an already-stepped or already-confirmed
   * tick (confirm-without-action + late propose spawned client-only balls → soft
   * PHYS despawn storm). */
  uint32_t tick = g_lock.local_send_tick;
  if (tick <= g_lock.sim_tick) {
    tick = g_lock.sim_tick + 1u;
  }
  if (tick <= g_lock.confirmed_tick) {
    tick = g_lock.confirmed_tick + 1u;
  }
  if (tick == 0u) {
    tick = 1u;
  }
  mod_lockstep_gen_local(tick);
  NgLockPeer *self = mod_lockstep_find_peer(g_lock.local_peer_id);
  if (!self || !mod_lockstep_slot_has(self, tick)) {
    return false;
  }
  NgLockSlot *s = &self->slots[tick % NG_LOCK_RING];
  // agent: composer-2.5 | 2026-08-01 | propose view only tip overwrite | cab054
  /* Tip may be re-proposed (same tick, view wins over stale); overwrite. */
  s->has_action = true;
  s->action_id = action_id;
  s->action_argc = argc;
  if (argc > 0) {
    memcpy(s->action_argv, argv, sizeof(float) * argc);
  } else {
    memset(s->action_argv, 0, sizeof(s->action_argv));
  }
  return true;
}

bool mod_lockstep_action_for(uint32_t peer_id, uint32_t tick, NgLockAction *out) {
  if (!out || tick == 0 || peer_id == 0) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  NgLockPeer *p = mod_lockstep_find_peer(peer_id);
  if (!p || !mod_lockstep_slot_has(p, tick)) {
    return false;
  }
  const NgLockSlot *s = &p->slots[tick % NG_LOCK_RING];
  if (!s->has_action) {
    return false;
  }
  out->present = 1;
  out->id = s->action_id;
  out->argc = s->action_argc;
  memcpy(out->argv, s->action_argv, sizeof(float) * s->action_argc);
  return true;
}

uint32_t mod_lockstep_peer_id_at(int index) {
  if (index < 0 || index >= g_lock.peer_count) {
    return 0;
  }
  return g_lock.peers[index].alive ? g_lock.peers[index].peer_id : 0;
}

// agent: composer-2.5 | 2026-07-30 | capture buttons into lock slots | 60a1ec
// agent: composer-2.5 | 2026-08-01 | slot action propose APIs | a8876f
// agent: composer-2.5 | 2026-08-01 | propose view only tip overwrite | cab054

int mod_lockstep_peers_need_catchup(uint32_t *out_peers, int max_peers) {
  if (!out_peers || max_peers <= 0 || g_lock.confirmed_tick == 0) {
    return 0;
  }
  int n = 0;
  const uint32_t floor = (g_lock.confirmed_tick > (uint32_t)NG_LOCK_CATCHUP_TICKS)
                             ? (g_lock.confirmed_tick - (uint32_t)NG_LOCK_CATCHUP_TICKS)
                             : 0u;
  for (int i = 0; i < g_lock.peer_count && n < max_peers; i++) {
    NgLockPeer *p = &g_lock.peers[i];
    if (!p->alive || p->peer_id == 0 || p->peer_id == g_lock.local_peer_id) {
      continue;
    }
    const uint32_t lag = p->highest_recv < p->ack_our ? p->highest_recv : p->ack_our;
    if (lag < floor) {
      out_peers[n++] = p->peer_id;
    }
  }
  return n;
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
// agent: composer-2.5 | 2026-07-31 | host confirm zero fill gate | 0e6e01
// agent: cursor-grok-4.5 | 2026-07-31 | remove dead synth path | f2b236
// agent: cursor-grok-4.5 | 2026-07-31 | apply authoritative resume roster | 6eea5d
// agent: cursor-grok-4.5 | 2026-07-31 | no mirror wire-learn peers | 55aad6
// agent: cursor-grok-4.5 | 2026-07-31 | no wire learn any side | b7c0ff
// agent: cursor-grok-4.5 | 2026-07-31 | lockstep net lost buffer | 939686
// agent: cursor-grok-4.5 | 2026-07-31 | host prune silent stall peers | a55e1c
// agent: composer-2.5 | 2026-07-31 | predict fill gate rollback | 5c8d67
// agent: composer-2.5 | 2026-07-31 | late input still heartbeat | 37caa0
// agent: composer-2.5 | 2026-07-31 | mirrors never self confirm | 6b113e
// agent: composer-2.5 | 2026-07-31 | confirm hist unsent bcast | 226e5b
// agent: composer-2.5 | 2026-07-31 | predict save ring depth | 752810
// agent: composer-2.5 | 2026-07-31 | drop mode b comments lock | 02a2b4
// agent: composer-2.5 | 2026-07-31 | lockstep stats and adapt | 68202b
// agent: composer-2.5 | 2026-08-01 | per-peer playout lockstep | ce507f
// agent: composer-2.5 | 2026-08-01 | predict allow gate save | d6bd83
// agent: composer-2.5 | 2026-08-01 | hybrid flag lockstep impl | 384379
// agent: composer-2.5 | 2026-08-01 | slot action propose APIs | a8876f
// agent: composer-2.5 | 2026-08-01 | propose view only tip overwrite | cab054
// agent: composer-2.5 | 2026-08-01 | propose future tip only | e6c793
// agent: composer-2.5 | 2026-08-01 | keep wire action on adopt | 0b6b21
// agent: composer-2.5 | 2026-08-01 | hash only confirmed tips | 2c31a3
