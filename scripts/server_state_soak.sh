#!/usr/bin/env bash
# agent: composer-2.5 | 2026-08-01 | server state soak script | 8419a0
# 2-remote cube (sim:server) soak - remotes stay connected; host keeps ticking.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build}"
SOAK_SEC="${NG_STATE_SOAK_SEC:-6}"
LOG=/tmp/ngame_server_state_soak.log
R1_LOG=/tmp/ngame_server_state_r1.log
R2_LOG=/tmp/ngame_server_state_r2.log

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
./ngame_server >"$LOG" 2>&1 &
SPID=$!
for _ in $(seq 1 100); do
  grep -q "server ready" "$LOG" 2>/dev/null && break
  sleep 0.05
done
grep -q "server ready" "$LOG"

BOOT=$(agent_line 27100 "scene cube")
echo "$BOOT" | grep -q "scene loaded: cube" || {
  echo "server-state: scene cube failed: $BOOT" >&2
  exit 1
}

./ngame --remote 127.0.0.1:27015 --agent-port 27111 >"$R1_LOG" 2>&1 &
R1_PID=$!
./ngame --remote 127.0.0.1:27015 --agent-port 27112 >"$R2_LOG" 2>&1 &
R2_PID=$!

# Remotes bind the agent port from REGISTER_ACK (27101+), not --agent-port.
ok=0
for _ in $(seq 1 200); do
  if grep -qE "peer connect id=2" "$LOG" 2>/dev/null; then
    R1=$(printf '{"cmd":"world_snapshot"}\n' | timeout 3 nc -q1 127.0.0.1 27101 2>/dev/null || true)
    R2=$(printf '{"cmd":"world_snapshot"}\n' | timeout 3 nc -q1 127.0.0.1 27102 2>/dev/null || true)
    if echo "$R1" | grep -q "scene=cube" && echo "$R2" | grep -q "scene=cube"; then
      ok=1
      break
    fi
  fi
  sleep 0.1
done
if [[ "$ok" != 1 ]]; then
  echo "server-state: remotes not ready" >&2
  tail -40 "$LOG" >&2
  exit 1
fi

sleep "$SOAK_SEC"

SNAP=$(printf '{"cmd":"world_snapshot"}\n' | timeout 3 nc -q1 127.0.0.1 27100 2>/dev/null || true)
echo "server-state snap: $SNAP"
echo "$SNAP" | grep -q "scene=cube"

R1=$(printf '{"cmd":"world_snapshot"}\n' | timeout 3 nc -q1 127.0.0.1 27101 2>/dev/null || true)
R2=$(printf '{"cmd":"world_snapshot"}\n' | timeout 3 nc -q1 127.0.0.1 27102 2>/dev/null || true)
echo "$R1" | grep -q "scene=cube"
echo "$R2" | grep -q "scene=cube"

if grep -q "note_desync\|lockstep: ERROR\|segfault\|Aborted" "$LOG" "$R1_LOG" "$R2_LOG" 2>/dev/null; then
  echo "server-state: error markers in logs" >&2
  exit 1
fi

echo "SERVER_STATE_SOAK ok scene=cube remotes=2 soak=${SOAK_SEC}s proto=STATE_UPDATE"
# agent: composer-2.5 | 2026-08-01 | server state soak script | 8419a0
