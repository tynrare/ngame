#!/usr/bin/env python3
# agent: composer-2.5 | 2026-08-02 | two client shoot mcp harness | 50e313
"""Two remotes on stacking; one client wire_input F; compare hashes + ball ids."""

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


def killall():
    subprocess.run(["killall", "-9", "ngame", "ngame_server"], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.4)


def wait_ready(log: Path, timeout=15.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if log.exists() and "server ready" in log.read_text(errors="ignore"):
            return
        time.sleep(0.1)
    raise RuntimeError(f"server not ready: {log}")


def parse_hash(text: str):
    m = re.search(r"hash=(0x[0-9a-fA-F]+).*confirmed=(\d+).*resim=(\d+)", text)
    if not m:
        m = re.search(
            r"confirmed=(\d+).*hash=(0x[0-9a-fA-F]+).*resim=(\d+)", text
        )
        if m:
            return m.group(2), int(m.group(1)), int(m.group(3))
        return None, None, None
    return m.group(1), int(m.group(2)), int(m.group(3))


def ball_ids(text: str):
    ids = []
    for m in re.finditer(r"id=(\d+)\s+key=\S*\s+desc=shoot_ball_e", text):
        ids.append(int(m.group(1)))
    sims = re.findall(r"desc=shoot_ball_e.*?sim=t(\d+)/p(\d+)/s(\d+)", text)
    ents = re.findall(r"server\[entities=(\d+)", text)
    return sorted(ids), sims, [int(x) for x in ents]


def main() -> int:
    killall()
    BUILD.mkdir(exist_ok=True)
    srv_log = Path("/tmp/ngame_shoot_srv.log")
    r1_log = Path("/tmp/ngame_shoot_r1.log")
    r2_log = Path("/tmp/ngame_shoot_r2.log")
    for p in (srv_log, r1_log, r2_log):
        p.write_text("")

    srv = subprocess.Popen(
        [str(BUILD / "ngame_server")],
        cwd=str(BUILD),
        stdout=srv_log.open("w"),
        stderr=subprocess.STDOUT,
    )
    wait_ready(srv_log)
    line(27100, "scene stacking")
    time.sleep(0.3)

    r1 = subprocess.Popen(
        [str(BUILD / "ngame"), "--remote", "127.0.0.1:27015", "--agent-port", "27111"],
        cwd=str(BUILD),
        stdout=r1_log.open("w"),
        stderr=subprocess.STDOUT,
    )
    r2 = subprocess.Popen(
        [str(BUILD / "ngame"), "--remote", "127.0.0.1:27015", "--agent-port", "27112"],
        cwd=str(BUILD),
        stdout=r2_log.open("w"),
        stderr=subprocess.STDOUT,
    )
    time.sleep(2.5)

    # Remotes bind REGISTER_ACK ports 27101/27102 typically.
    ports = []
    for p in range(27101, 27110):
        try:
            t = cmd(p, "world_snapshot")
        except OSError:
            continue
        if "stacking" in t or "scene=" in t:
            ports.append(p)
    if len(ports) < 2:
        print("FAIL: need 2 remote MCP ports, got", ports, file=sys.stderr)
        killall()
        return 1
    a, b = ports[0], ports[1]
    print(f"remotes MCP {a} {b}")

    # One client presses F (edge via short hold).
    print(line(a, "wire_input F frames=8"))
    time.sleep(2.0)

    ha = cmd(a, "lockstep_hash")
    hb = cmd(b, "lockstep_hash")
    print("A", ha)
    print("B", hb)
    pa = cmd(a, "phys_debug")
    pb = cmd(b, "phys_debug")
    ids_a, sims_a, ent_a = ball_ids(pa)
    ids_b, sims_b, ent_b = ball_ids(pb)
    print("balls A", ids_a, sims_a, "ents", ent_a)
    print("balls B", ids_b, sims_b, "ents", ent_b)

    # Observability: action/confirm lines
    for label, log in (("r1", r1_log), ("r2", r2_log), ("srv", srv_log)):
        txt = log.read_text(errors="ignore")
        hits = [ln for ln in txt.splitlines() if "action:" in ln or "confirm action mismatch" in ln
                or "import despawn" in ln or "rollback confirm" in ln]
        print(f"--- {label} events ({len(hits)}) ---")
        for ln in hits[-30:]:
            print(ln)

    ok = True
    if ids_a != ids_b:
        print("FAIL: ball id set mismatch", ids_a, ids_b)
        ok = False
    h1, c1, r1s = parse_hash(ha)
    h2, c2, r2s = parse_hash(hb)
    if h1 and h2 and c1 == c2 and h1 != h2:
        print("FAIL: hash diverge at confirmed", c1, h1, h2)
        ok = False
    if ok and not ids_a and not (ent_a and ent_a[0] >= 32):
        print("WARN: no shoot_ball / entity bump (propose may have failed)")
        # Still useful if logs show mismatch/despawn
        if any("confirm action mismatch" in r1_log.read_text(errors="ignore") for _ in [0]):
            pass

    killall()
    r1.wait(timeout=3)
    r2.wait(timeout=3)
    srv.wait(timeout=3)
    if ok and (ids_a or (ent_a and ent_b and ent_a[0] >= 32 and ent_a == ent_b)):
        print("TWO_CLIENT_SHOOT ok balls", ids_a, "ents", ent_a)
        return 0
    if ok:
        print("TWO_CLIENT_SHOOT inconclusive (no balls)")
        return 2
    print("TWO_CLIENT_SHOOT fail")
    return 1


if __name__ == "__main__":
    sys.exit(main())
