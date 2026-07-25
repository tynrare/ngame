#!/usr/bin/env bash
# agent: composer-2.5 | 2026-07-25 | full server validation suite | f6a04e
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
killall -9 ngame_server ng_test_net 2>/dev/null || true
for _ in $(seq 1 30); do
  ss -ulnp 2>/dev/null | grep -q ':27015' || break
  killall -9 ngame_server 2>/dev/null || true
  sleep 0.1
done
sleep 0.3

cd "$BUILD"
cmake --build . --target ngame_server ng_test_net 2>&1 | tail -3

start_server() {
  ./ngame_server > /tmp/ngame_validate.log 2>&1 &
  SPID=$!
  for _ in $(seq 1 100); do
    grep -q "server ready" /tmp/ngame_validate.log 2>/dev/null && return 0
    sleep 0.05
  done
  echo "server failed to start" >&2
  cat /tmp/ngame_validate.log >&2
  return 1
}

stop_server() {
  kill -9 "$SPID" 2>/dev/null || true
  killall -9 ngame_server 2>/dev/null || true
  # agent: composer-2.5 | 2026-07-25 | validate restart cleanup | 7a540e
  for _ in $(seq 1 30); do
    ss -ulnp 2>/dev/null | grep -q ':27015' || break
    sleep 0.1
  done
  sleep 0.3
}

trap stop_server EXIT

start_server

echo "== agent multi-line =="
OUT=$(printf '{"cmd":"world_snapshot"}\n' | timeout 10 nc -q1 127.0.0.1 27100 || true)
OUT2=$(printf '{"line":"scene sphere"}\n' | timeout 10 nc -q1 127.0.0.1 27100 || true)
OUT="$OUT"$'\n'"$OUT2"
echo "$OUT"
echo "$OUT" | grep -q "scene=sphere"
echo "$OUT" | grep -q "scene loaded: sphere"

stop_server
start_server

echo "== mcp agent bridge =="
MCP_OUT=$(python3 -c "
import importlib.util
spec = importlib.util.spec_from_file_location('mcp_agent', '$ROOT/tools/mcp_agent.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
print(m.agent_request({'cmd': 'world_snapshot'})['text'])
")
echo "$MCP_OUT"
echo "$MCP_OUT" | grep -q "scene="

stop_server
start_server

echo "== enet scene cube =="
./ng_test_net 127.0.0.1 27015 | tee /tmp/ngame_net.out
grep -qE 'REPLY: scene loaded: cube|EVENT: cube' /tmp/ngame_net.out

stop_server
start_server

echo "== websocket scene cube =="
python3 "$ROOT/tools/ws_smoke.py" 127.0.0.1 27016 | tee /tmp/ngame_ws.out
grep -q 'scene loaded: cube' /tmp/ngame_ws.out

stop_server

if [[ -f "$ROOT/build-web/ngame.html" ]]; then
  echo "== web build artifact =="
  test -f "$ROOT/build-web/ngame.html"
  echo "build-web/ngame.html ok"
else
  echo "== web build =="
  "$ROOT/build-web.sh" 2>&1 | tail -5
  test -f "$ROOT/build-web/ngame.html"
fi

echo "ALL OK"
