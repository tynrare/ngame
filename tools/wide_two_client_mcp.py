#!/usr/bin/env python3
# agent: composer-2.5 | 2026-07-29 | wide two-client transform mcp test | 3c54dd
"""Drive both gateway clients with MCP wire_* and assert entity_transforms sync."""

from __future__ import annotations

import json
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
HOST = "127.0.0.1"


def agent(port: int, payload: dict, timeout: float = 3.0) -> dict:
    data = (json.dumps(payload, separators=(",", ":")) + "\n").encode()
    with socket.create_connection((HOST, port), timeout=timeout) as sock:
        sock.sendall(data)
        buf = b""
        while b"\n" not in buf:
            chunk = sock.recv(4096)
            if not chunk:
                break
            buf += chunk
    line = buf.split(b"\n", 1)[0].decode().strip()
    return json.loads(line) if line else {"ok": False, "error": "empty"}


def cmd(port: int, name: str) -> str:
    return agent(port, {"cmd": name}).get("text", "")


def line(port: int, text: str) -> str:
    return agent(port, {"line": text}).get("text", "")


def parse_xform(text: str):
    m = re.search(
        r"pos=([-\d.]+),([-\d.]+),([-\d.]+) rot=([-\d.]+),([-\d.]+),([-\d.]+)",
        text,
    )
    if not m:
        return None
    return tuple(float(v) for v in m.groups())


def near(a, b, eps=0.08):
    return a is not None and b is not None and abs(a - b) <= eps


def wait_ports(timeout=15.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        found = []
        for p in range(27100, 27110):
            try:
                t = cmd(p, "world_snapshot")
            except OSError:
                continue
            if "gateway upstream=1" in t:
                found.append(p)
        if len(found) >= 2:
            return found[0], found[1]
        time.sleep(0.2)
    raise RuntimeError("gateway MCP ports not found")


def assert_sync(label, xa, xb, check_pos=True, check_rot=True):
    if xa is None or xb is None:
        raise AssertionError(f"{label}: missing xform A={xa} B={xb}")
    if check_pos and not (near(xa[0], xb[0]) and near(xa[2], xb[2])):
        raise AssertionError(f"{label}: pos desync A={xa[:3]} B={xb[:3]}")
    if check_rot and not (near(xa[4], xb[4], 0.12) and near(xa[3], xb[3], 0.12)):
        raise AssertionError(f"{label}: rot desync A={xa[3:6]} B={xb[3:6]}")
    print(f"OK {label}: A={xa} B={xb}")


def main() -> int:
    subprocess.run(["killall", "-9", "ngame_server", "ngame"], check=False)
    time.sleep(0.4)
    server = subprocess.Popen(
        [str(BUILD / "ngame_server")],
        cwd=BUILD,
        stdout=open("/tmp/ng_wide_root.log", "w"),
        stderr=subprocess.STDOUT,
    )
    time.sleep(0.8)
    clients = []
    for name in ("a", "b"):
        clients.append(
            subprocess.Popen(
                ["xvfb-run", "-a", str(BUILD / "ngame"), "--remote", "127.0.0.1:27015"],
                cwd=BUILD,
                stdout=open(f"/tmp/ng_wide_{name}.log", "w"),
                stderr=subprocess.STDOUT,
            )
        )
        time.sleep(1.2)

    try:
        a, b = wait_ports()
        print(f"ports A={a} B={b}")
        print(line(a, "scene cube"))
        time.sleep(1.2)

        # frames=N holds for N/60 seconds; wait past that so wires don't overlap.
        def hold(frames: int) -> None:
            time.sleep(frames / 60.0 + 0.35)

        # 1) A rotates past former clamp (~0.57), both must keep climbing
        line(a, "wire_input D frames=240")
        hold(240)
        xa = parse_xform(cmd(a, "entity_transforms"))
        xb = parse_xform(cmd(b, "entity_transforms"))
        assert_sync("A-rotate", xa, xb)
        if xa[4] <= 0.7:
            raise AssertionError(f"rotation lock suspected rot_y={xa[4]} (want >0.7)")
        pos_after_rot = xa[:3]

        # 2) B moves mouse; A must follow
        line(b, "wire_mouse 100 120 frames=90")
        hold(90)
        xa = parse_xform(cmd(a, "entity_transforms"))
        xb = parse_xform(cmd(b, "entity_transforms"))
        assert_sync("B-move", xa, xb)
        if abs(xb[0] + 6.696) > 0.4:
            raise AssertionError(f"B mouse did not move cube enough pos={xb[:3]}")

        # 3) A moves mouse; B must follow
        line(a, "wire_mouse 700 360 frames=90")
        hold(90)
        xa = parse_xform(cmd(a, "entity_transforms"))
        xb = parse_xform(cmd(b, "entity_transforms"))
        assert_sync("A-move", xa, xb)
        if abs(xa[0] - 2.249) > 0.4:
            raise AssertionError(f"A mouse did not move cube enough pos={xa[:3]}")
        pos_after_a_move = xa[:3]

        # 4) B rotates other axis while A idle — position must stay put
        line(b, "wire_input W frames=180")
        hold(180)
        xa = parse_xform(cmd(a, "entity_transforms"))
        xb = parse_xform(cmd(b, "entity_transforms"))
        assert_sync("B-rotate-x", xa, xb)
        if abs(xb[3]) < 0.2:
            raise AssertionError(f"B W rotation too small rot_x={xb[3]}")
        if not (near(xa[0], pos_after_a_move[0], 0.08) and near(xa[2], pos_after_a_move[2], 0.08)):
            raise AssertionError(
                f"rotate authored position {pos_after_a_move} -> {xa[:3]}"
            )

        # 5) idle: neither should keep rewriting (spot check stability)
        s1a = parse_xform(cmd(a, "entity_transforms"))
        time.sleep(1.0)
        s2a = parse_xform(cmd(a, "entity_transforms"))
        s2b = parse_xform(cmd(b, "entity_transforms"))
        assert_sync("idle", s2a, s2b)
        if not (near(s1a[0], s2a[0], 0.05) and near(s1a[4], s2a[4], 0.05)):
            raise AssertionError(f"idle drift {s1a} -> {s2a}")
        _ = pos_after_rot

        print("WIDE_TWO_CLIENT_MCP PASS")
        return 0
    finally:
        for p in clients:
            p.kill()
        server.kill()
        subprocess.run(["killall", "-9", "ngame_server", "ngame"], check=False)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001
        print(f"WIDE_TWO_CLIENT_MCP FAIL: {exc}", file=sys.stderr)
        subprocess.run(["killall", "-9", "ngame_server", "ngame"], check=False)
        raise SystemExit(1)

# agent: composer-2.5 | 2026-07-29 | wide two-client transform mcp test | 3c54dd
