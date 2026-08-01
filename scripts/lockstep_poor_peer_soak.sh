#!/usr/bin/env bash
# agent: composer-2.5 | 2026-08-01 | poor peer soak script | bc2f6a
# 2-remote physics soak: drop+delay, then kill/rejoin one remote by stable name (ghost rebind).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build}"
DROP="${NG_LOCK_SIM_DROP:-15}"
DELAY_MS="${NG_LOCK_SIM_DELAY_MS:-50}"
SOAK_SEC="${NG_LOCK_SOAK_SEC:-8}"
MIN_CONFIRMED="${NG_LOCK_SOAK_MIN_CONFIRMED:-40}"
LOG=/tmp/ngame_lockstep_poor_peer.log
R1_LOG=/tmp/ngame_lockstep_poor_r1.log
R2_LOG=/tmp/ngame_lockstep_poor_r2.log

killall -9 ngame_server ngame 2>/dev/null || true
for _ in $(seq 1 40); do
  ss -ulnp 2>/dev/null | grep -q ':27015' || break
  sleep 0.05
done

cd "$BUILD"
cmake --build . --target ngame ngame_server -j"$(nproc)" >/dev/null

agent_line() {
  local port="$1"
  local line="$2"
  printf '{"line":"%s"}\n' "$line" | timeout 4 nc -q1 127.0.0.1 "$port" 2>/dev/null || true
}

cleanup() {
  kill -9 "${R1_PID:-}" "${R2_PID:-}" "${SPID:-}" 2>/dev/null || true
  killall -9 ngame_server ngame 2>/dev/null || true
}
trap cleanup EXIT

: >"$LOG"
NG_LOCK_SIM_DROP="$DROP" NG_LOCK_SIM_DELAY_MS="$DELAY_MS" ./ngame_server >"$LOG" 2>&1 &
SPID=$!
for _ in $(seq 1 100); do
  grep -q "server ready" "$LOG" 2>/dev/null && break
  sleep 0.05
done
grep -q "server ready" "$LOG"

BOOT=$(agent_line 27100 "scene physics")
echo "$BOOT" | grep -q "scene loaded: physics" || {
  echo "poor-peer: scene physics failed: $BOOT" >&2
  exit 1
}

NG_LOCK_PEER_NAME=poor-r1 ./ngame --remote 127.0.0.1:27015 --agent-port 27111 >"$R1_LOG" 2>&1 &
R1_PID=$!
NG_LOCK_PEER_NAME=poor-r2 ./ngame --remote 127.0.0.1:27015 --agent-port 27112 >"$R2_LOG" 2>&1 &
R2_PID=$!

ok=0
for _ in $(seq 1 200); do
  HASH=$(agent_line 27100 "lockstep_hash")
  if echo "$HASH" | grep -q 'active=1' && echo "$HASH" | grep -qE 'peers=[2-9]'; then
    ok=1
    break
  fi
  sleep 0.1
done
if [[ "$ok" != 1 ]]; then
  echo "poor-peer: peers not ready: $HASH" >&2
  tail -40 "$LOG" >&2
  exit 1
fi

sleep "$SOAK_SEC"
HASH_BEFORE=$(agent_line 27100 "lockstep_hash")
CONF_BEFORE=$(echo "$HASH_BEFORE" | sed -n 's/.*confirmed=\([0-9][0-9]*\).*/\1/p')
echo "poor-peer before rejoin: $HASH_BEFORE"

# Kill r2; host should ghost seat (no LOCK_PAUSE for survivors).
kill -9 "$R2_PID" 2>/dev/null || true
R2_PID=
ok=0
for _ in $(seq 1 80); do
  if grep -q "lockstep: ghost peer=" "$LOG" 2>/dev/null; then
    ok=1
    break
  fi
  sleep 0.1
done
if [[ "$ok" != 1 ]]; then
  echo "poor-peer: expected ghost after disconnect" >&2
  tail -80 "$LOG" >&2
  exit 1
fi

# Rejoin same name within PRUNE_SEC → soft PHYS rebind.
: >"$R2_LOG"
NG_LOCK_PEER_NAME=poor-r2 ./ngame --remote 127.0.0.1:27015 --agent-port 27112 >"$R2_LOG" 2>&1 &
R2_PID=$!

ok=0
for _ in $(seq 1 100); do
  if grep -q "lockstep: REBIND" "$LOG" 2>/dev/null; then
    ok=1
    break
  fi
  sleep 0.1
done
if [[ "$ok" != 1 ]]; then
  echo "poor-peer: REBIND not seen" >&2
  tail -80 "$LOG" >&2
  exit 1
fi
grep -q "soft PHYS\|REBIND PHYS\|lockstep: REBIND" "$LOG"

sleep 4
HASH=$(agent_line 27100 "lockstep_hash")
echo "poor-peer after rejoin: $HASH"
echo "$HASH" | grep -q 'active=1'
echo "$HASH" | grep -qE 'playout=[0-9]+'
CONF=$(echo "$HASH" | sed -n 's/.*confirmed=\([0-9][0-9]*\).*/\1/p')
if [[ -z "$CONF" || -z "$CONF_BEFORE" ]]; then
  echo "poor-peer: missing confirmed" >&2
  exit 1
fi
if [[ "$CONF" -lt "$MIN_CONFIRMED" ]]; then
  echo "poor-peer: confirmed=$CONF < min $MIN_CONFIRMED" >&2
  exit 1
fi
if [[ "$CONF" -le "$CONF_BEFORE" ]]; then
  echo "poor-peer: confirmed did not advance ($CONF_BEFORE → $CONF)" >&2
  exit 1
fi

if grep -q "note_desync\|permanent STALL\|lockstep: ERROR" "$LOG"; then
  echo "poor-peer: freeze/desync markers" >&2
  grep -E "note_desync|permanent STALL|lockstep: ERROR" "$LOG" >&2 || true
  exit 1
fi

echo "LOCKSTEP_POOR_PEER_SOAK ok drop=${DROP}% delay=${DELAY_MS}ms confirmed=$CONF rebind=1"
# agent: composer-2.5 | 2026-08-01 | poor peer soak script | bc2f6a
