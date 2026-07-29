#!/usr/bin/env bash
# agent: composer-2.5 | 2026-07-25 | full server validation suite | f6a04e
# agent: composer-2.5 | 2026-07-25 | single server validate | 51bd3a
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
cmake --build . --target ngame ngame_server ng_test_net ng_two_client_smoke ng_cmd_bandwidth ng_cmd_latency ng_cmd_loopback_latency ng_embed_smoke ng_scene_js_smoke 2>&1 | tail -3

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
echo "$OUT" | grep -q "entities=1"
echo "$OUT" | grep -q "scene loaded: sphere"

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
echo "$MCP_OUT" | grep -q "local scene="

echo "== enet scene cube (same server) =="
./ng_test_net 127.0.0.1 27015 | tee /tmp/ngame_net.out
grep -q 'REPLY: scene loaded: cube' /tmp/ngame_net.out
grep -q 'SESSION scene=cube' /tmp/ngame_net.out

echo "== scene js smoke =="
./ng_scene_js_smoke

echo "== two client session =="
./ng_two_client_smoke 127.0.0.1 27015

echo "== bandwidth sample =="
BW_OUT=$(./ng_cmd_bandwidth 127.0.0.1 27015)
echo "$BW_OUT"
echo "$BW_OUT" | grep -q 'BW_SNAP_BYTES'

echo "== cmd latency enet (local path) =="
LAT_OUT=$(./ng_cmd_latency 127.0.0.1 27015)
echo "$LAT_OUT"
echo "$LAT_OUT" | grep -q 'CMD_LATENCY_MS'
MS=$(echo "$LAT_OUT" | awk '/CMD_LATENCY_MS/{print $2}')
awk -v ms="$MS" 'BEGIN { exit !(ms+0 <= 100) }'

echo "== cmd latency loopback (tooling smoke) =="
./ng_cmd_loopback_latency

echo "== websocket scene cube (same server) =="
python3 "$ROOT/tools/ws_smoke.py" 127.0.0.1 27016 | tee /tmp/ngame_ws.out
grep -q 'scene loaded: cube' /tmp/ngame_ws.out

echo "== embedded loopback (native) =="
EMBED_OUT=$(./ng_embed_smoke)
echo "$EMBED_OUT"
echo "$EMBED_OUT" | grep -q 'EMBED_SNAPSHOT scene=cube'

stop_server

echo "== gateway solo smoke =="
./ngame --solo > /tmp/ngame_solo.log 2>&1 &
SOLO_PID=$!
SOLO_OUT=""
for _ in $(seq 1 100); do
  SOLO_OUT=$(printf '{"cmd":"world_snapshot"}\n' | timeout 3 nc -q1 127.0.0.1 27100 2>/dev/null || true)
  echo "$SOLO_OUT" | grep -q "local scene=sphere" && break
  sleep 0.05
done
kill -9 "$SOLO_PID" 2>/dev/null || true
echo "$SOLO_OUT"
echo "$SOLO_OUT" | grep -q "local scene=sphere"
echo "$SOLO_OUT" | grep -q "view scene=sphere loaded=1"

echo "== gateway local smoke =="
./ngame --local > /tmp/ngame_local.log 2>&1 &
LOCAL_PID=$!
ROOT_OUT=""
DEP_OUT=""
for _ in $(seq 1 100); do
  ROOT_OUT=$(printf '{"cmd":"world_snapshot"}\n' | timeout 3 nc -q1 127.0.0.1 27100 2>/dev/null || true)
  DEP_OUT=$(printf '{"cmd":"world_snapshot"}\n' | timeout 3 nc -q1 127.0.0.1 27101 2>/dev/null || true)
  echo "$ROOT_OUT" | grep -q "scene=sphere" && echo "$DEP_OUT" | grep -q "render scene=sphere" && break
  sleep 0.05
done
echo "root: $ROOT_OUT"
echo "dep:  $DEP_OUT"
echo "$ROOT_OUT" | grep -q "scene=sphere"
echo "$DEP_OUT" | grep -q "render scene=sphere"

echo "== gateway local scene boot cmd =="
BOOT_OUT=""
for _ in $(seq 1 60); do
  BOOT_OUT=$(printf '{"line":"scene boot"}\n' | timeout 4 nc -q1 127.0.0.1 27101 2>/dev/null || true)
  echo "$BOOT_OUT" | grep -q "scene loaded: boot" && break
  sleep 0.05
done
echo "$BOOT_OUT"
echo "$BOOT_OUT" | grep -q "scene loaded: boot"

SNAP_OUT=$(printf '{"cmd":"world_snapshot"}\n' | timeout 4 nc -q1 127.0.0.1 27101 2>/dev/null || true)
echo "$SNAP_OUT"
echo "$SNAP_OUT" | grep -q "root scene=boot"
echo "$SNAP_OUT" | grep -q "view scene=boot"
echo "$SNAP_OUT" | grep -q "view scene=boot loaded=1"
echo "$SNAP_OUT" | grep -q "visible=0"
echo "$SNAP_OUT" | grep -q "bg=080c14"
echo "$SNAP_OUT" | grep -q "gateway upstream=1"
echo "$SNAP_OUT" | grep -q "ready=1"

echo "== gateway local scene sphere switch =="
SPHERE_OUT=""
for _ in $(seq 1 60); do
  SPHERE_OUT=$(printf '{"line":"scene sphere"}\n' | timeout 4 nc -q1 127.0.0.1 27101 2>/dev/null || true)
  echo "$SPHERE_OUT" | grep -q "scene loaded: sphere" && break
  sleep 0.05
done
echo "$SPHERE_OUT"
echo "$SPHERE_OUT" | grep -q "scene loaded: sphere"

SPHERE_SNAP=$(printf '{"cmd":"world_snapshot"}\n' | timeout 4 nc -q1 127.0.0.1 27101 2>/dev/null || true)
echo "$SPHERE_SNAP"
echo "$SPHERE_SNAP" | grep -q "root scene=sphere"
echo "$SPHERE_SNAP" | grep -q "view scene=sphere loaded=1"
echo "$SPHERE_SNAP" | grep -q "render scene=sphere"
echo "$SPHERE_SNAP" | grep -q "visible=1"
echo "$SPHERE_SNAP" | grep -q "bg=0c1430"

kill -9 "$LOCAL_PID" 2>/dev/null || true
killall -9 ngame_server 2>/dev/null || true

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
# agent: composer-2.5 | 2026-07-29 | gateway scene boot mcp smoke | b0c1d2
# agent: composer-2.5 | 2026-07-29 | boot smoke entity expectations | 2c818e
# agent: composer-2.5 | 2026-07-29 | boot routes startup validate | f998b8
