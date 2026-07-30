// agent: composer-2.5 | 2026-07-29 | lockstep clock module | a8bff9
// agent: composer-2.5 | 2026-07-30 | lockstep syncing gate APIs | 9516cf
#ifndef MOD_SCENE_LOCKSTEP_H
#define MOD_SCENE_LOCKSTEP_H

#include <stdbool.h>
#include <stdint.h>

#define NG_LOCK_RING 128
#define NG_LOCK_PLAYOUT_TICKS 6
#ifndef NG_LOCK_PEER_MAX
#define NG_LOCK_PEER_MAX 8
#endif

typedef enum NgLockGate {
  NG_LOCK_GATE_GO = 0,     /* run physics fixed step */
  NG_LOCK_GATE_BUFFER = 1, /* consume wall time for playout inputs only */
  NG_LOCK_GATE_STALL = 2,  /* keep accumulator; missing peer input / syncing */
} NgLockGate;

void mod_lockstep_reset(void);
void mod_lockstep_set_active(bool active);
bool mod_lockstep_active(void);
void mod_lockstep_set_local_peer(uint32_t peer_id);
void mod_lockstep_clear_peers(void);
void mod_lockstep_add_peer(uint32_t peer_id);
bool mod_lockstep_needs_join_sync(void);

void mod_lockstep_begin_sync(uint32_t tick);
void mod_lockstep_end_sync(void);
bool mod_lockstep_syncing(void);
void mod_lockstep_set_sim_tick(uint32_t tick);
void mod_lockstep_await_phys(bool await);
bool mod_lockstep_awaiting_phys(void);

NgLockGate mod_lockstep_gate(void);
void mod_lockstep_on_stepped(uint32_t tick, uint32_t hash);

void mod_lockstep_store_remote_input(uint32_t peer_id, uint32_t tick, uint8_t bits);
void mod_lockstep_store_ack(uint32_t peer_id, uint32_t ack_tick);

uint32_t mod_lockstep_sim_tick(void);
uint32_t mod_lockstep_local_ack(void);
uint32_t mod_lockstep_last_hash(void);
uint32_t mod_lockstep_last_hash_tick(void);
uint32_t mod_lockstep_local_peer_id(void);

int mod_lockstep_fill_send_window(uint32_t *out_base_tick, uint8_t *out_bits, int max_count);
uint32_t mod_lockstep_highest_recv_contiguous(void);
void mod_lockstep_debug(uint32_t *out_send, int *out_peers, int *out_started, uint32_t *out_peer);
void mod_lockstep_debug_full(uint32_t *out_send, int *out_peers, int *out_started, uint32_t *out_peer,
                             int *out_syncing, int *out_await, uint32_t *out_sim);

#endif
// agent: composer-2.5 | 2026-07-29 | lockstep clock module | a8bff9
// agent: composer-2.5 | 2026-07-30 | lockstep gate diag | b71030
// agent: composer-2.5 | 2026-07-30 | lockstep syncing gate APIs | 9516cf
// agent: composer-2.5 | 2026-07-30 | lockstep join fixes logging | 5d65f6
