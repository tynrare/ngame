// agent: composer-2.5 | 2026-07-29 | lockstep clock module | a8bff9
// agent: composer-2.5 | 2026-07-30 | lockstep syncing gate APIs | 9516cf
// agent: composer-2.5 | 2026-07-30 | bits_for and step tick APIs | 8f051c
// agent: composer-2.5 | 2026-07-30 | remove peer on disconnect | 00aa6c
// agent: composer-2.5 | 2026-07-30 | clock owner APIs | a6571f
#ifndef MOD_SCENE_LOCKSTEP_H
#define MOD_SCENE_LOCKSTEP_H

#include <stdbool.h>
#include <stdint.h>

#define NG_LOCK_RING 128
/* Gaffer Deterministic Lockstep: ~100ms playout @ 60Hz hides jitter; too small → hitch/STALL. */
// agent: composer-2.5 | 2026-07-31 | gaffer playout 6 ticks | 6e3db6
#define NG_LOCK_PLAYOUT_TICKS 6
/* Host: peer missing next-sim input longer than this (wall) → treat as leave. */
// agent: cursor-grok-4.5 | 2026-07-31 | host prune silent stall peers | a55e1c
#define NG_LOCK_SILENT_SEC 1.0
/* Mode B: confirm next tick after this wait if any peer input still missing (zeros).
 * Must exceed playout+jitter; 100ms matched playout and zero-filled live peers → prune. */
// agent: composer-2.5 | 2026-07-31 | confirm deadline playout | 7b5fce
#define NG_LOCK_CONFIRM_SEC 0.35
/* Mode B: peer ack/recv lag behind confirmed → PHYS catchup. */
#define NG_LOCK_CATCHUP_TICKS 45
/* Mode B: max local predict ticks past confirmed (Save ring). */
#define NG_LOCK_PREDICT_MAX 8
/* Host: consecutive LOCK_HASH mismatches → soft PHYS resync for that peer. */
// agent: composer-2.5 | 2026-07-31 | desync streak phys APIs | 88dfc6
#define NG_LOCK_DESYNC_PHYS_STREAK 2
/* Host keeps recent confirms so flush can send every tick (gate used to
 * confirm silently; only last was resent → clients permanently gapped). */
// agent: composer-2.5 | 2026-07-31 | confirm hist APIs | b075d5
#define NG_LOCK_CONFIRM_HIST 48
#ifndef NG_LOCK_PEER_MAX
#define NG_LOCK_PEER_MAX 8
#endif
// agent: cursor-grok-4.5 | 2026-07-31 | remove dead synth path | 2600bd

// agent: composer-2.5 | 2026-07-30 | lockstep playout configurable | 9afea6
void mod_lockstep_set_playout_ticks(uint32_t ticks);
uint32_t mod_lockstep_playout_ticks(void);
typedef enum NgLockGate {
  NG_LOCK_GATE_GO = 0,     /* run physics fixed step */
  NG_LOCK_GATE_BUFFER = 1, /* consume wall time for playout inputs only */
  NG_LOCK_GATE_STALL = 2,  /* keep accumulator; missing peer input / syncing */
} NgLockGate;

void mod_lockstep_reset(void);
void mod_lockstep_set_active(bool active);
bool mod_lockstep_active(void);
void mod_lockstep_set_clock_owner(bool owner);
bool mod_lockstep_is_clock_owner(void);
void mod_lockstep_note_roster(void);
bool mod_lockstep_roster_ok(void);
/* True once every alive peer has sent ≥1 LOCK_INPUT (cold-start gate). */
bool mod_lockstep_all_peers_live(void);
void mod_lockstep_set_local_peer(uint32_t peer_id);
void mod_lockstep_clear_peers(void);
void mod_lockstep_add_peer(uint32_t peer_id);
void mod_lockstep_remove_peer(uint32_t peer_id);
/* RESUME roster is authoritative — drop peers not listed (disconnect). */
// agent: cursor-grok-4.5 | 2026-07-31 | apply authoritative resume roster | 6f64df
void mod_lockstep_apply_roster(const uint8_t *peer_ids, int count);
bool mod_lockstep_needs_join_sync(void);
/* Mirror: upstream lost — clear roster, BUFFER until new SESSION/RESUME. */
// agent: cursor-grok-4.5 | 2026-07-31 | lockstep net lost buffer | 939686
void mod_lockstep_on_net_lost(void);
/* Clock owner: drop peers silent through stall grace; returns count dropped. */
int mod_lockstep_prune_silent_peers(uint32_t *out_dropped, int max_dropped);

void mod_lockstep_begin_sync(uint32_t tick);
void mod_lockstep_end_sync(void);
bool mod_lockstep_syncing(void);
void mod_lockstep_set_sim_tick(uint32_t tick);
void mod_lockstep_await_phys(bool await);
bool mod_lockstep_awaiting_phys(void);
void mod_lockstep_note_desync(void);

NgLockGate mod_lockstep_gate(void);
void mod_lockstep_on_stepped(uint32_t tick, uint32_t hash);

void mod_lockstep_store_remote_input(uint32_t peer_id, uint32_t tick, uint8_t bits);
void mod_lockstep_store_ack(uint32_t peer_id, uint32_t ack_tick);

uint32_t mod_lockstep_sim_tick(void);
uint32_t mod_lockstep_step_tick(void);
void mod_lockstep_set_step_tick(uint32_t tick);
uint8_t mod_lockstep_bits_for(uint32_t peer_id, uint32_t tick);
uint8_t mod_lockstep_bits_or(uint32_t tick);
bool mod_lockstep_have_input(uint32_t peer_id, uint32_t tick);
int mod_lockstep_peer_count(void);
uint32_t mod_lockstep_local_ack(void);
uint32_t mod_lockstep_last_hash(void);
uint32_t mod_lockstep_last_hash_tick(void);
uint32_t mod_lockstep_local_peer_id(void);

int mod_lockstep_fill_send_window(uint32_t *out_base_tick, uint8_t *out_bits, int max_count);
uint32_t mod_lockstep_highest_recv_contiguous(void);
void mod_lockstep_debug(uint32_t *out_send, int *out_peers, int *out_started, uint32_t *out_peer);
void mod_lockstep_debug_full(uint32_t *out_send, int *out_peers, int *out_started, uint32_t *out_peer,
                             int *out_syncing, int *out_await, uint32_t *out_sim);

/* Mode B: host-confirmed input timeline. */
// agent: composer-2.5 | 2026-07-31 | mode b confirm APIs | 17e3ab
typedef struct NgLockConfirmPkt NgLockConfirmPkt;
uint32_t mod_lockstep_confirmed_tick(void);
void mod_lockstep_set_confirmed_tick(uint32_t tick);
bool mod_lockstep_apply_confirm(const NgLockConfirmPkt *pkt);
/* Clock owner: produce next confirm (all_have or deadline zero-fill). */
bool mod_lockstep_host_try_confirm(NgLockConfirmPkt *out);
/* Last known bits for peer (prediction hold); 0 if never seen. */
uint8_t mod_lockstep_last_bits(uint32_t peer_id);
/* Peers whose contiguous recv lags confirmed by CATCHUP_TICKS. */
int mod_lockstep_peers_need_catchup(uint32_t *out_peers, int max_peers);
bool mod_lockstep_copy_last_confirm(NgLockConfirmPkt *out);
/* Host net: next confirm not yet marked sent (tick == bcast+1). */
bool mod_lockstep_next_unsent_confirm(NgLockConfirmPkt *out);
void mod_lockstep_mark_confirm_sent(uint32_t tick);
uint32_t mod_lockstep_resim_to(void);
void mod_lockstep_clear_resim(void);
/* Soft PHYS snap: set sim+confirmed, wipe future input slots, clear save ring. */
// agent: composer-2.5 | 2026-07-31 | desync streak phys APIs | 88dfc6
void mod_lockstep_on_soft_phys(uint32_t tick);

#endif
// agent: composer-2.5 | 2026-07-29 | lockstep clock module | a8bff9
// agent: composer-2.5 | 2026-07-30 | lockstep gate diag | b71030
// agent: composer-2.5 | 2026-07-30 | lockstep syncing gate APIs | 9516cf
// agent: composer-2.5 | 2026-07-30 | lockstep join fixes logging | 5d65f6
// agent: composer-2.5 | 2026-07-30 | lockstep playout configurable | 9afea6
// agent: composer-2.5 | 2026-07-30 | bits_for and step tick APIs | 8f051c
// agent: composer-2.5 | 2026-07-30 | remove peer on disconnect | 00aa6c
// agent: composer-2.5 | 2026-07-30 | peer count accessor | 6cf6aa
// agent: composer-2.5 | 2026-07-30 | clock owner APIs | a6571f
// agent: composer-2.5 | 2026-07-30 | wait all_have before zero-fill | 24fa25
// agent: composer-2.5 | 2026-07-31 | gaffer playout 6 ticks | 6e3db6
// agent: cursor-grok-4.5 | 2026-07-31 | remove dead synth path | 2600bd
// agent: cursor-grok-4.5 | 2026-07-31 | apply authoritative resume roster | 6f64df
// agent: cursor-grok-4.5 | 2026-07-31 | lockstep net lost buffer | 939686
// agent: cursor-grok-4.5 | 2026-07-31 | host prune silent stall peers | a55e1c
// agent: composer-2.5 | 2026-07-31 | mode b confirm APIs | 17e3ab
// agent: composer-2.5 | 2026-07-31 | predict APIs | bb7d40
// agent: composer-2.5 | 2026-07-31 | confirm deadline playout | 7b5fce
// agent: composer-2.5 | 2026-07-31 | confirm hist APIs | b075d5
// agent: composer-2.5 | 2026-07-31 | desync streak phys APIs | 88dfc6
