#!/usr/bin/env bash
# agent: composer-2.5 | 2026-08-09 | topology matrix harness | 7b6880
# Nested lockstep / facet compose matrix (T_solo … T_pack).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build}"
LOG_DIR="${TOPOLOGY_LOG_DIR:-/tmp/ngame_topology}"
SCENE="${TOPOLOGY_SCENE:-stacking}"
SOAK_S="${TOPOLOGY_SOAK_S:-3}"
XVFB=( )
if command -v xvfb-run >/dev/null 2>&1; then
  XVFB=(xvfb-run -a)
fi

mkdir -p "$LOG_DIR"
cd "$BUILD"
cmake --build . --target ngame ngame_server -j"$(nproc)" >/dev/null

kill_all() {
  killall -9 ngame ngame_server 2>/dev/null || true
  for p in 27015 27017 27019 27021; do
    for _ in $(seq 1 40); do
      ss -ulnp 2>/dev/null | grep -q ":$p" || break
      sleep 0.05
    done
  done
}

agent_cmd() {
  local port="$1"
  local payload="$2"
  printf '%s\n' "$payload" | timeout 4 nc -q1 127.0.0.1 "$port" 2>/dev/null || true
}

agent_line() {
  agent_cmd "$1" "{\"line\":\"$2\"}"
}

agent_snap() {
  agent_cmd "$1" '{"cmd":"world_snapshot"}'
}

wait_ready_log() {
  local log="$1"
  local n="${2:-120}"
  for _ in $(seq 1 "$n"); do
    grep -q "server ready" "$log" 2>/dev/null && return 0
    sleep 0.05
  done
  echo "fail: server not ready ($log)" >&2
  tail -40 "$log" >&2 || true
  return 1
}

wait_lock_peers() {
  local port="$1"
  local min_peers="$2"
  local hash=""
  for _ in $(seq 1 200); do
    hash=$(agent_line "$port" "lockstep_hash")
    if echo "$hash" | grep -q 'active=1'; then
      local peers
      peers=$(echo "$hash" | sed -n 's/.*peers=\([0-9][0-9]*\).*/\1/p')
      if [[ -n "$peers" && "$peers" -ge "$min_peers" ]]; then
        echo "$hash"
        return 0
      fi
    fi
    sleep 0.1
  done
  echo "fail: lockstep peers<$min_peers on :$port ($hash)" >&2
  return 1
}

wait_confirmed() {
  local port="$1"
  local min_c="$2"
  local hash=""
  for _ in $(seq 1 120); do
    hash=$(agent_line "$port" "lockstep_hash")
    local conf
    conf=$(echo "$hash" | sed -n 's/.*confirmed=\([0-9][0-9]*\).*/\1/p')
    if [[ -n "$conf" && "$conf" -ge "$min_c" ]]; then
      echo "$hash"
      return 0
    fi
    sleep 0.1
  done
  echo "fail: confirmed<$min_c on :$port ($hash)" >&2
  return 1
}

assert_no_confirm_clear_storm() {
  local log="$1"
  local n
  n=$(grep -c "confirm clear action" "$log" 2>/dev/null || true)
  n=${n:-0}
  if [[ "$n" -gt 8 ]]; then
    echo "fail: confirm clear storm count=$n in $log" >&2
    return 1
  fi
}

start_remote() {
  local hostport="$1"
  local agent="$2"
  local log="$3"
  "${XVFB[@]}" ./ngame --remote "$hostport" --agent-port "$agent" >"$log" 2>&1 &
  echo $!
}

run_T_solo() {
  echo "== T_solo =="
  kill_all
  local log="$LOG_DIR/T_solo.log"
  ./ngame --solo >"$log" 2>&1 &
  local pid=$!
  local snap=""
  for _ in $(seq 1 100); do
    snap=$(agent_snap 27100)
    echo "$snap" | grep -q "local scene=" && break
    sleep 0.05
  done
  kill -9 "$pid" 2>/dev/null || true
  echo "$snap" | grep -q "local scene="
  echo "T_solo ok"
}

run_T_root() {
  local nclients="$1"
  local id="T_root${nclients}"
  echo "== $id =="
  kill_all
  local slog="$LOG_DIR/${id}_server.log"
  ./ngame --server --port 27015 >"$slog" 2>&1 &
  local spid=$!
  wait_ready_log "$slog"
  agent_line 27100 "scene $SCENE" | grep -q "scene loaded"
  local pids=()
  local i=0
  while [[ $i -lt $nclients ]]; do
    local ap=$((27111 + i))
    pids+=("$(start_remote 127.0.0.1:27015 "$ap" "$LOG_DIR/${id}_c${i}.log")")
    i=$((i + 1))
  done
  wait_lock_peers 27100 "$nclients" >/dev/null
  sleep "$SOAK_S"
  wait_confirmed 27100 10 >/dev/null
  assert_no_confirm_clear_storm "$slog"
  kill -9 "$spid" "${pids[@]}" 2>/dev/null || true
  echo "$id ok"
}

run_T_proxy2() {
  echo "== T_proxy2 =="
  kill_all
  local alog="$LOG_DIR/T_proxy2_A.log"
  local blog="$LOG_DIR/T_proxy2_B.log"
  ./ngame --server --port 27015 >"$alog" 2>&1 &
  local apid=$!
  wait_ready_log "$alog"
  agent_line 27100 "scene $SCENE" | grep -q "scene loaded"
  ./ngame --server --port 27017 --connect 127.0.0.1:27015 >"$blog" 2>&1 &
  local bpid=$!
  wait_ready_log "$blog"
  # B MCP may be 27100 (if A died) or next free — prefer probe after B ready.
  local b_mcp=27101
  for p in 27100 27101 27102 27103; do
    if agent_snap "$p" | grep -q "scene="; then
      # Prefer non-A: A already has stacking; B starts on boot scene until commanded.
      if [[ "$p" != 27100 ]]; then
        b_mcp=$p
        break
      fi
    fi
  done
  # If only 27100 answers, A owns it — B must have probed.
  for p in 27101 27102 27103 27104; do
    if agent_line "$p" "status" | grep -q .; then
      b_mcp=$p
      break
    fi
  done
  agent_line "$b_mcp" "scene $SCENE" | grep -q "scene loaded" || {
    echo "fail: B scene load on mcp=$b_mcp" >&2
    tail -40 "$blog" >&2
    exit 1
  }
  local c0 c1
  c0=$(start_remote 127.0.0.1:27017 27121 "$LOG_DIR/T_proxy2_c0.log")
  c1=$(start_remote 127.0.0.1:27017 27122 "$LOG_DIR/T_proxy2_c1.log")
  wait_lock_peers "$b_mcp" 2 >/dev/null
  sleep "$SOAK_S"
  wait_confirmed "$b_mcp" 8 >/dev/null
  # Uplink: A sees proxy as one peer (plus maybe host) — at least 1.
  wait_lock_peers 27100 1 >/dev/null
  assert_no_confirm_clear_storm "$blog"
  # Optional short soak with ping/loss on B (env).
  if [[ "${TOPOLOGY_PROXY_SOAK:-0}" == 1 ]]; then
    kill -9 "$c0" "$c1" 2>/dev/null || true
    sleep 0.3
    ./ngame --server --port 27017 --connect 127.0.0.1:27015 --ping 25 --loss 5 \
      >"$LOG_DIR/T_proxy2_B_soak.log" 2>&1 &
    bpid=$!
    wait_ready_log "$LOG_DIR/T_proxy2_B_soak.log"
  fi
  kill -9 "$apid" "$bpid" "$c0" "$c1" 2>/dev/null || true
  echo "T_proxy2 ok (B mcp=$b_mcp)"
}

run_T_proxy_mixed() {
  echo "== T_proxy_mixed =="
  kill_all
  local alog="$LOG_DIR/T_mixed_A.log"
  local blog="$LOG_DIR/T_mixed_B.log"
  ./ngame --server --port 27015 >"$alog" 2>&1 &
  local apid=$!
  wait_ready_log "$alog"
  agent_line 27100 "scene $SCENE" | grep -q "scene loaded"
  ./ngame --server --port 27017 --connect 127.0.0.1:27015 >"$blog" 2>&1 &
  local bpid=$!
  wait_ready_log "$blog"
  local b_mcp=27101
  for p in 27101 27102 27103 27104; do
    if agent_line "$p" "status" | grep -q .; then
      b_mcp=$p
      break
    fi
  done
  agent_line "$b_mcp" "scene $SCENE" | grep -q "scene loaded"
  local ca cb0 cb1
  ca=$(start_remote 127.0.0.1:27015 27131 "$LOG_DIR/T_mixed_ca.log")
  cb0=$(start_remote 127.0.0.1:27017 27132 "$LOG_DIR/T_mixed_cb0.log")
  cb1=$(start_remote 127.0.0.1:27017 27133 "$LOG_DIR/T_mixed_cb1.log")
  wait_lock_peers 27100 2 >/dev/null   # A: proxy + direct client
  wait_lock_peers "$b_mcp" 2 >/dev/null # B: two remotes
  sleep "$SOAK_S"
  kill -9 "$apid" "$bpid" "$ca" "$cb0" "$cb1" 2>/dev/null || true
  echo "T_proxy_mixed ok"
}

run_T_chain() {
  echo "== T_chain =="
  kill_all
  local alog="$LOG_DIR/T_chain_A.log"
  local blog="$LOG_DIR/T_chain_B.log"
  local clog="$LOG_DIR/T_chain_C.log"
  ./ngame --server --port 27015 >"$alog" 2>&1 &
  local apid=$!
  wait_ready_log "$alog"
  agent_line 27100 "scene $SCENE" | grep -q "scene loaded"
  ./ngame --server --port 27017 --connect 127.0.0.1:27015 >"$blog" 2>&1 &
  local bpid=$!
  wait_ready_log "$blog"
  ./ngame --server --port 27019 --connect 127.0.0.1:27017 >"$clog" 2>&1 &
  local cpid=$!
  wait_ready_log "$clog"
  local c_mcp=27102
  for p in 27101 27102 27103 27104 27105; do
    if agent_line "$p" "status" | grep -q .; then
      # last free wins — prefer highest that answers after C starts
      c_mcp=$p
    fi
  done
  agent_line "$c_mcp" "scene $SCENE" | grep -q "scene loaded" || {
    # try all
    local ok=0
    for p in 27101 27102 27103 27104 27105; do
      if agent_line "$p" "scene $SCENE" | grep -q "scene loaded"; then
        c_mcp=$p
        ok=1
        break
      fi
    done
    [[ "$ok" == 1 ]] || { echo "fail: C scene" >&2; exit 1; }
  }
  local r0 r1
  r0=$(start_remote 127.0.0.1:27019 27141 "$LOG_DIR/T_chain_r0.log")
  r1=$(start_remote 127.0.0.1:27019 27142 "$LOG_DIR/T_chain_r1.log")
  wait_lock_peers "$c_mcp" 2 >/dev/null
  sleep "$SOAK_S"
  wait_lock_peers 27100 1 >/dev/null
  kill -9 "$apid" "$bpid" "$cpid" "$r0" "$r1" 2>/dev/null || true
  echo "T_chain ok (C mcp=$c_mcp)"
}

run_T_pack() {
  echo "== T_pack =="
  kill_all
  local alog="$LOG_DIR/T_pack_A.log"
  local blog="$LOG_DIR/T_pack_B.log"
  ./ngame_server --port 27015 >"$alog" 2>&1 &
  local apid=$!
  wait_ready_log "$alog"
  agent_line 27100 "scene $SCENE" | grep -q "scene loaded"
  ./ngame --server --port 27017 --connect 127.0.0.1:27015 >"$blog" 2>&1 &
  local bpid=$!
  wait_ready_log "$blog"
  local b_mcp=27101
  for p in 27101 27102 27103; do
    if agent_line "$p" "status" | grep -q .; then
      b_mcp=$p
      break
    fi
  done
  agent_line "$b_mcp" "scene $SCENE" | grep -q "scene loaded"
  local c0 c1
  c0=$(start_remote 127.0.0.1:27017 27151 "$LOG_DIR/T_pack_c0.log")
  c1=$(start_remote 127.0.0.1:27017 27152 "$LOG_DIR/T_pack_c1.log")
  wait_lock_peers "$b_mcp" 2 >/dev/null
  sleep "$SOAK_S"
  wait_lock_peers 27100 1 >/dev/null
  kill -9 "$apid" "$bpid" "$c0" "$c1" 2>/dev/null || true
  echo "T_pack ok"
}

trap kill_all EXIT

ONLY="${TOPOLOGY_ONLY:-}"
if [[ -z "$ONLY" || "$ONLY" == "T_solo" ]]; then run_T_solo; fi
if [[ -z "$ONLY" || "$ONLY" == "T_root1" ]]; then run_T_root 1; fi
if [[ -z "$ONLY" || "$ONLY" == "T_root2" ]]; then run_T_root 2; fi
if [[ -z "$ONLY" || "$ONLY" == "T_proxy2" ]]; then run_T_proxy2; fi
if [[ -z "$ONLY" || "$ONLY" == "T_proxy_mixed" ]]; then run_T_proxy_mixed; fi
if [[ -z "$ONLY" || "$ONLY" == "T_chain" ]]; then run_T_chain; fi
if [[ -z "$ONLY" || "$ONLY" == "T_pack" ]]; then run_T_pack; fi

kill_all
echo "TOPOLOGY_MATRIX ok"
# agent: composer-2.5 | 2026-08-09 | topology matrix harness | 7b6880
