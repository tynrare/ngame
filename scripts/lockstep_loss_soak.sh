#!/usr/bin/env bash
# agent: composer-2.5 | 2026-07-31 | lockstep loss soak script | 12fb0e
# agent: composer-2.5 | 2026-07-31 | soak delay adapt validate | b68a5d
# 2-remote physics lockstep soak under NG_LOCK_SIM_DROP + optional NG_LOCK_SIM_DELAY_MS.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build}"
DROP="${NG_LOCK_SIM_DROP:-15}"
DELAY_MS="${NG_LOCK_SIM_DELAY_MS:-50}"
SOAK_SEC="${NG_LOCK_SOAK_SEC:-12}"
MIN_CONFIRMED="${NG_LOCK_SOAK_MIN_CONFIRMED:-40}"
LOG=/tmp/ngame_lockstep_loss_soak.log
R1_LOG=/tmp/ngame_lockstep_loss_r1.log
R2_LOG=/tmp/ngame_lockstep_loss_r2.log

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
grep -q "sim drop=${DROP}%" "$LOG" || {
  # env read lazily on first LOCK_INPUT — force by scene+peers; also accept log later
  true
}

BOOT=$(agent_line 27100 "scene physics")
echo "$BOOT" | grep -q "scene loaded: physics" || {
  echo "soak: scene physics failed: $BOOT" >&2
  exit 1
}

./ngame --remote 127.0.0.1:27015 --agent-port 27111 >"$R1_LOG" 2>&1 &
R1_PID=$!
./ngame --remote 127.0.0.1:27015 --agent-port 27112 >"$R2_LOG" 2>&1 &
R2_PID=$!

# Wait remotes upstream + lockstep peers.
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
  echo "soak: peers not ready: $HASH" >&2
  tail -40 "$LOG" >&2
  exit 1
fi

# Ensure drop/delay banners appeared (first LOCK_INPUT).
for _ in $(seq 1 50); do
  grep -q "sim drop=${DROP}%" "$LOG" && break
  sleep 0.1
done
grep -q "sim drop=${DROP}%" "$LOG"
if [[ "$DELAY_MS" -gt 0 ]]; then
  for _ in $(seq 1 50); do
    grep -q "sim delay=${DELAY_MS}ms" "$LOG" && break
    sleep 0.1
  done
  grep -q "sim delay=${DELAY_MS}ms" "$LOG"
fi

sleep "$SOAK_SEC"

HASH=$(agent_line 27100 "lockstep_hash")
echo "soak hash: $HASH"
echo "$HASH" | grep -q 'active=1'
echo "$HASH" | grep -qE 'playout=[0-9]+'
echo "$HASH" | grep -qE 'zf=[0-9]+'

CONF=$(echo "$HASH" | sed -n 's/.*confirmed=\([0-9][0-9]*\).*/\1/p')
if [[ -z "$CONF" ]]; then
  echo "soak: missing confirmed in: $HASH" >&2
  exit 1
fi
if [[ "$CONF" -lt "$MIN_CONFIRMED" ]]; then
  echo "soak: confirmed=$CONF < min $MIN_CONFIRMED under drop=${DROP}% delay=${DELAY_MS}ms" >&2
  exit 1
fi

if grep -q "note_desync\|permanent STALL\|lockstep: ERROR" "$LOG"; then
  echo "soak: freeze/desync markers in server log" >&2
  grep -E "note_desync|permanent STALL|lockstep: ERROR" "$LOG" >&2 || true
  exit 1
fi

PLAYOUT=$(echo "$HASH" | sed -n 's/.*playout=\([0-9][0-9]*\).*/\1/p')
ZF=$(echo "$HASH" | sed -n 's/.*zf=\([0-9][0-9]*\).*/\1/p')
echo "LOCKSTEP_LOSS_SOAK ok drop=${DROP}% delay=${DELAY_MS}ms confirmed=$CONF playout=${PLAYOUT:-?} zf=${ZF:-?} soak=${SOAK_SEC}s"
# agent: composer-2.5 | 2026-07-31 | lockstep loss soak script | 12fb0e
# agent: composer-2.5 | 2026-07-31 | soak delay adapt validate | b68a5d
