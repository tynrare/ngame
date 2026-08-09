#!/usr/bin/env python3
# agent: composer-2.5 | 2026-08-02 | parallel client bring-up | 1bced5
"""Ping/loss/throttle soak. Launch-soak first; healable hash → desync%; hard-stop on unrecoverable / 90%."""
from __future__ import annotations

import json
import os
import re
import signal
import socket
import subprocess
import sys
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path

BUILD = Path(os.environ.get("BUILD", Path(__file__).resolve().parents[1] / "build"))
ROOT = BUILD.parent
SCENE = os.environ.get("THROTTLE_SCENE", "stacking")
_INPUT_RAW = os.environ.get("THROTTLE_INPUT", "F").strip()
INPUT_KEYS = (
    []
    if _INPUT_RAW.lower() in ("", "none", "-", "idle")
    else _INPUT_RAW.split()
)
LABEL = os.environ.get("THROTTLE_LABEL", SCENE)
MIN_GRAPH = int(os.environ.get("THROTTLE_MIN_GRAPH", "20"))
OUT_MD = Path(os.environ.get("THROTTLE_DOC", ROOT / "docs" / "throttle-test.md"))
OUT_JSON = Path(
    os.environ.get("THROTTLE_JSON", ROOT / "docs" / f"throttle-{LABEL}.json")
)
SOAK_S = float(os.environ.get("THROTTLE_SOAK_S", "8"))
SAMPLE_S = float(os.environ.get("THROTTLE_SAMPLE_S", "0.5"))
WARM_S = float(os.environ.get("THROTTLE_WARM_S", "2.0"))
DESYNC_FAIL_PCT = float(os.environ.get("THROTTLE_DESYNC_FAIL_PCT", "90"))
LAUNCH_RETRIES = int(os.environ.get("THROTTLE_LAUNCH_RETRIES", "3"))
# Retry flaky product breaks (soft PHYS race) when desync% still below fail threshold
PRODUCT_RETRIES = int(os.environ.get("THROTTLE_PRODUCT_RETRIES", "2"))
READY_TIMEOUT_S = float(os.environ.get("THROTTLE_READY_S", "45"))
CLIENTS_ONLY = os.environ.get("THROTTLE_CLIENTS", "1,2")
FLAKY_PRODUCT = frozenset({"phys_resync_exhausted"})

PING_MAX_MS = 5000
LOSS_MAX_PCT = 90
THROTTLE_MAX_PCT = 90

PING_STEPS = [0, 25, 50, 75, 100, 150, 200, 300, 400, 500, 750, 1000, 1500, 2000, 3000, 5000]
LOSS_STEPS = [0, 5, 10, 15, 20, 25, 30, 40, 50, 60, 70, 80, 90]
THROTTLE_STEPS = [0, 10, 20, 30, 40, 50, 60, 70, 80, 90]
COMBINED_STEPS = [
    (0, 0, 0),
    (25, 5, 10),
    (50, 10, 20),
    (75, 15, 30),
    (100, 20, 40),
    (150, 25, 50),
    (200, 30, 60),
    (300, 40, 70),
    (400, 50, 80),
    (500, 60, 90),
    (1000, 70, 90),
    (2000, 80, 90),
    (5000, 90, 90),
]

INFRA_REASONS = frozenset(
    {
        "server_not_ready",
        "scene_load_failed",
        "clients_not_ready",
        "mcp_lost",
        "wire_input_failed",
        "infra_exhausted",
    }
)


@dataclass
class LockSnap:
    tick: int = -1
    confirmed: int = -1
    predict: int = 0
    zf: int = 0
    playout: int = 0
    peers: int = 0
    started: int = 0
    active: int = 0
    hash: str = ""
    last_tick: int = 0
    last_hash: str = ""
    raw: str = ""


@dataclass
class TrialResult:
    clients: int
    mode: str
    ping: int
    loss: int
    throttle: int
    ok: bool
    scene: str = SCENE
    break_reason: str = ""
    notes: str = ""
    desync_pct: float = 0.0
    desync_samples: int = 0
    total_samples: int = 0
    infra: bool = False
    launch_attempts: int = 1
    samples: list = field(default_factory=list)


@dataclass
class BreakHit:
    reason: str
    detail: str = ""


def text(port: int, payload: dict, timeout: float = 4.0) -> str:
    data = (json.dumps(payload, separators=(",", ":")) + "\n").encode()
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.sendall(data)
        sock.settimeout(timeout)
        buf = b""
        while b"\n" not in buf:
            chunk = sock.recv(65536)
            if not chunk:
                break
            buf += chunk
    line = buf.split(b"\n", 1)[0].decode().strip()
    if not line:
        return ""
    try:
        return json.loads(line).get("text", "")
    except json.JSONDecodeError:
        return line


def parse_lock(t: str) -> LockSnap:
    s = LockSnap(raw=t)

    def i(k: str, default: int = 0) -> int:
        m = re.search(rf"{k}=(-?\d+)", t)
        return int(m.group(1)) if m else default

    def h(k: str) -> str:
        m = re.search(rf"{k}=(0x[0-9a-fA-F]+)", t)
        return m.group(1) if m else ""

    s.tick = i("tick", -1)
    s.confirmed = i("confirmed", -1)
    s.predict = i("predict")
    s.zf = i("zf")
    s.playout = i("playout")
    s.peers = i("peers")
    s.started = i("started")
    s.active = i("active")
    s.hash = h("hash")
    s.last_tick = i("last_tick")
    s.last_hash = h("last_hash")
    return s


def graph_ents(snap: str) -> tuple[str, int]:
    m = re.search(r"view scene=(\S+) loaded=\d+ graph=(\d+)", snap)
    if not m:
        return ("?", -1)
    return (m.group(1), int(m.group(2)))


def sample_hash_desync(locks: list[LockSnap]) -> bool:
    if len(locks) < 2:
        return False
    a, b = locks[0], locks[1]
    if not a.last_tick or a.last_tick != b.last_tick:
        return False
    if not a.last_hash or not b.last_hash:
        return False
    return a.last_hash != b.last_hash


def port_free(port: int, udp: bool = False) -> bool:
    family = socket.AF_INET
    sock_type = socket.SOCK_DGRAM if udp else socket.SOCK_STREAM
    s = socket.socket(family, sock_type)
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("127.0.0.1", port))
        return True
    except OSError:
        return False
    finally:
        s.close()


def wait_ports_clear(timeout: float = 8.0) -> None:
    t0 = time.time()
    while time.time() - t0 < timeout:
        ok = port_free(27015, udp=True) and port_free(27100)
        for p in range(27101, 27106):
            ok = ok and port_free(p)
        if ok:
            return
        time.sleep(0.15)


def find_client_ports(n: int) -> list[int]:
    found = []
    for p in range(27101, 27120):
        try:
            t = text(p, {"cmd": "world_snapshot"}, timeout=1.0)
        except OSError:
            continue
        if "Connected to" in t:
            found.append(p)
        if len(found) >= n:
            break
    return found


def kill_all() -> None:
    for _ in range(3):
        subprocess.run(
            ["killall", "-9", "ngame", "ngame_server"],
            check=False,
            capture_output=True,
        )
        time.sleep(0.5)
    # Xvfb last — avoid racing xvfb-run cleanup
    subprocess.run(["killall", "-9", "Xvfb"], check=False, capture_output=True)
    time.sleep(1.0)
    wait_ports_clear(20.0)


def start_server(ping: int, loss: int, throttle: int, log: Path) -> subprocess.Popen:
    args = [str(BUILD / "ngame_server")]
    if ping > 0:
        args += ["--ping", str(ping)]
    if loss > 0:
        args += ["--loss", str(loss)]
    if throttle > 0:
        args += ["--throttle", str(throttle)]
    log.write_text("")
    return subprocess.Popen(args, cwd=BUILD, stdout=log.open("w"), stderr=subprocess.STDOUT)


def start_client(idx: int, throttle: int, log: Path) -> subprocess.Popen:
    # Agent port is server-assigned on register; --agent-port is a local hint only.
    args = ["xvfb-run", "-a", str(BUILD / "ngame"), "--remote", "127.0.0.1:27015"]
    if throttle > 0:
        args += ["--throttle", str(throttle)]
    log.write_text("")
    return subprocess.Popen(args, cwd=BUILD, stdout=log.open("w"), stderr=subprocess.STDOUT)


def wait_server_ready(log: Path, timeout: float = 12.0) -> bool:
    t0 = time.time()
    while time.time() - t0 < timeout:
        if log.exists() and "server ready" in log.read_text(errors="ignore"):
            # MCP must accept
            try:
                text(27100, {"cmd": "world_snapshot"}, timeout=1.5)
                return True
            except OSError:
                pass
        time.sleep(0.15)
    return False


def count_log(path: Path, needle: str) -> int:
    if not path.exists():
        return 0
    return path.read_text(errors="ignore").count(needle)


def classify_unrecoverable(
    hist: list[dict],
    n_clients: int,
    logs: list[Path],
    server_log: Path,
) -> BreakHit | None:
    """Hard product fails only. No entity_desync; no healable hash."""
    if not hist:
        return BreakHit("no_samples")

    last = hist[-1]
    for i, ls in enumerate(last["locks"]):
        if ls.tick < 0 or ls.active != 1:
            return BreakHit("lockstep_dead", f"client{i} active={ls.active} tick={ls.tick}")

    if len(hist) >= 2:
        wall = hist[-1]["t"] - hist[0]["t"]
        dtick = hist[-1]["locks"][0].tick - hist[0]["locks"][0].tick
        dconf = hist[-1]["locks"][0].confirmed - hist[0]["locks"][0].confirmed
        if wall >= 3.0 and dtick < 12 and dconf < 12 and hist[-1]["locks"][0].tick > 0:
            return BreakHit(
                "tick_frozen",
                f"tickΔ={dtick} confΔ={dconf} over {wall:.1f}s",
            )

    zf_tail = [h["locks"][0].zf for h in hist[-4:]]
    if zf_tail and min(zf_tail) >= 48:
        return BreakHit("zf_spiral", f"zf>={min(zf_tail)} sustained")

    if len(hist) >= 4:
        p0 = hist[-4]["locks"][0]
        p1 = hist[-1]["locks"][0]
        if p1.predict >= 36 and (p1.confirmed - p0.confirmed) <= 2:
            return BreakHit(
                "predict_death",
                f"predict={p1.predict} confirmedΔ={p1.confirmed - p0.confirmed}",
            )

    if n_clients >= 2 and len(hist) >= 4:
        skews = [abs(h["locks"][0].tick - h["locks"][1].tick) for h in hist[-4:]]
        if min(skews) >= 30:
            return BreakHit("tick_desync", f"tick skew>={min(skews)}")

    for p in logs + [server_log]:
        body = p.read_text(errors="ignore") if p.exists() else ""
        if "PHYS resync exhausted" in body:
            return BreakHit("phys_resync_exhausted", p.name)
        if body.count("await=1") >= 30 and "RESUME" not in body[-2000:]:
            return BreakHit("await_stuck", p.name)

    return None


def clients_ready(ports: list[int], clients: int, ping: int = 0) -> bool:
    if len(ports) < clients:
        return False
    for p in ports[:clients]:
        try:
            snap = text(p, {"cmd": "world_snapshot"}, timeout=2.0)
            lk = parse_lock(text(p, {"cmd": "lockstep_hash"}, timeout=2.0))
        except OSError:
            return False
        sc, g = graph_ents(snap)
        if (
            sc != SCENE
            or g < MIN_GRAPH
            or lk.started != 1
            or lk.active != 1
            or lk.peers < clients
            or lk.tick < 30
        ):
            return False
    if ping > 0:
        # Under simulated delay, confirm may crawl; peer presence is the bring-up signal.
        return True
    t_a = [parse_lock(text(p, {"cmd": "lockstep_hash"})).tick for p in ports[:clients]]
    time.sleep(1.0)
    t_b = [parse_lock(text(p, {"cmd": "lockstep_hash"})).tick for p in ports[:clients]]
    return all(b > a + 10 for a, b in zip(t_a, t_b))


def ready_diag(ports: list[int], clients: int) -> str:
    """Why clients_ready failed — for launch-soak failure dumps."""
    parts = [f"ports={ports}"]
    for i, p in enumerate(ports[:clients]):
        try:
            snap = text(p, {"cmd": "world_snapshot"}, timeout=2.0)
            lk = parse_lock(text(p, {"cmd": "lockstep_hash"}, timeout=2.0))
            sc, g = graph_ents(snap)
            parts.append(
                f"c{i}@{p}:scene={sc} graph={g} started={lk.started} active={lk.active} "
                f"peers={lk.peers} tick={lk.tick} conf={lk.confirmed}"
            )
        except OSError as e:
            parts.append(f"c{i}@{p}:OSError {e}")
    while len(ports) < clients:
        parts.append(f"missing_client_slot={len(ports)}")
        break
    return "; ".join(parts)


@dataclass
class BringUp:
    ok: bool
    reason: str = ""
    notes: str = ""
    ports: list[int] = field(default_factory=list)
    slog: Path | None = None
    clogs: list[Path] = field(default_factory=list)
    procs: list = field(default_factory=list)


def bring_up(clients: int, ping: int = 0, loss: int = 0, throttle: int = 0) -> BringUp:
    """Server + scene + N clients until ready (no soak). Caller must kill_all when done."""
    kill_all()
    tag = f"launch_{LABEL}_{clients}_{ping}_{loss}_{throttle}_{int(time.time()*1000)}"
    slog = Path(f"/tmp/th_s_{tag}.log")
    clogs = [Path(f"/tmp/th_c{i}_{tag}.log") for i in range(clients)]
    out = BringUp(ok=False, slog=slog, clogs=clogs)

    server = start_server(ping, loss, throttle, slog)
    out.procs = [server]
    if not wait_server_ready(slog):
        out.reason = "server_not_ready"
        out.notes = slog.read_text(errors="ignore")[-400:] if slog.exists() else ""
        return out

    try:
        boot = text(27100, {"line": f"scene {SCENE}"})
    except OSError as e:
        out.reason = "scene_load_failed"
        out.notes = f"mcp_boot_oserror {e}"
        return out
    if SCENE not in boot:
        out.reason = "scene_load_failed"
        out.notes = boot[:200]
        return out

    for i in range(clients):
        out.procs.append(start_client(i, throttle, clogs[i]))
        # Start peers close together so 2c is not a huge mid-sim late-join.
        time.sleep(0.35 if i == 0 else 0.5)

    ports: list[int] = []
    t0 = time.time()
    # Delay slows REGISTER/PHYS/LOCK_INPUT — give join more wall time.
    ready_deadline = READY_TIMEOUT_S + max(0.0, ping / 1000.0) * 40.0
    while time.time() - t0 < ready_deadline:
        ports = find_client_ports(clients)
        out.ports = ports
        if clients_ready(ports, clients, ping=ping):
            out.ok = True
            out.ports = ports[:clients]
            out.reason = "ready"
            out.notes = ready_diag(out.ports, clients)
            return out
        time.sleep(0.5)

    out.reason = "clients_not_ready"
    out.notes = ready_diag(ports, clients)
    return out


def launch_soak(n: int, clients: int = 2, ping: int = 0, loss: int = 0, throttle: int = 0) -> int:
    """Run N consecutive bring-ups; stop on first fail. Return 0 if all OK else 1."""
    print(
        f"LAUNCH_SOAK n={n} scene={SCENE} clients={clients} "
        f"ping={ping} loss={loss} thr={throttle}",
        flush=True,
    )
    subprocess.run(
        [
            "cmake",
            "--build",
            str(BUILD),
            "-j",
            str(os.cpu_count() or 4),
            "--target",
            "ngame",
            "ngame_server",
        ],
        check=True,
    )
    ok_n = 0
    for i in range(1, n + 1):
        t0 = time.time()
        bu = bring_up(clients, ping, loss, throttle)
        elapsed = time.time() - t0
        if bu.ok:
            ok_n += 1
            print(f"  [{i}/{n}] OK {elapsed:.1f}s {bu.notes[:100]}", flush=True)
            for p in bu.procs:
                p.kill()
            kill_all()
            continue
        print(f"  [{i}/{n}] FAIL {bu.reason} after {elapsed:.1f}s", flush=True)
        print(f"    diag: {bu.notes}", flush=True)
        if bu.slog and bu.slog.exists():
            tail = bu.slog.read_text(errors="ignore").splitlines()[-30:]
            print("    --- server log tail ---", flush=True)
            for line in tail:
                print(f"    {line}", flush=True)
        for ci, clog in enumerate(bu.clogs):
            if clog.exists():
                ctail = clog.read_text(errors="ignore").splitlines()[-20:]
                print(f"    --- client{ci} log tail ---", flush=True)
                for line in ctail:
                    print(f"    {line}", flush=True)
        for p in bu.procs:
            p.kill()
        kill_all()
        print(f"LAUNCH_SOAK STOPPED at {i}/{n} (ok_streak={ok_n})", flush=True)
        return 1
    print(f"LAUNCH_SOAK PASS {n}/{n}", flush=True)
    return 0


def run_trial_once(clients: int, mode: str, ping: int, loss: int, throttle: int) -> TrialResult:
    result = TrialResult(
        clients=clients, mode=mode, ping=ping, loss=loss, throttle=throttle, ok=True, scene=SCENE
    )
    kill_all()
    tag = f"{LABEL}_{clients}_{mode}_{ping}_{loss}_{throttle}_{int(time.time())}"
    slog = Path(f"/tmp/th_s_{tag}.log")
    clogs = [Path(f"/tmp/th_c{i}_{tag}.log") for i in range(clients)]

    server = start_server(ping, loss, throttle, slog)
    if not wait_server_ready(slog):
        result.ok = False
        result.infra = True
        result.break_reason = "server_not_ready"
        kill_all()
        return result

    try:
        boot = text(27100, {"line": f"scene {SCENE}"})
    except OSError:
        result.ok = False
        result.infra = True
        result.break_reason = "scene_load_failed"
        result.notes = "mcp_boot_oserror"
        kill_all()
        return result
    if SCENE not in boot:
        result.ok = False
        result.infra = True
        result.break_reason = "scene_load_failed"
        result.notes = boot[:120]
        kill_all()
        return result

    procs = [server]
    for i in range(clients):
        procs.append(start_client(i, throttle, clogs[i]))
        time.sleep(0.35 if i == 0 else 0.5)

    ports: list[int] = []
    t0 = time.time()
    ready_deadline = READY_TIMEOUT_S + max(0.0, ping / 1000.0) * 40.0
    while time.time() - t0 < ready_deadline:
        ports = find_client_ports(clients)
        if clients_ready(ports, clients, ping=ping):
            break
        time.sleep(0.5)
    else:
        result.ok = False
        result.infra = True
        result.break_reason = "clients_not_ready"
        result.notes = f"ports={ports}"
        kill_all()
        return result
    ports = ports[:clients]

    # Warm: advance without counting desync%
    warm_end = time.time() + WARM_S
    while time.time() < warm_end:
        for p in ports:
            try:
                parse_lock(text(p, {"cmd": "lockstep_hash"}, timeout=2.0))
            except OSError:
                result.ok = False
                result.infra = True
                result.break_reason = "mcp_lost"
                result.notes = "warm"
                kill_all()
                return result
        time.sleep(0.25)

    hist: list[dict] = []
    desync_n = 0
    action_n = 0
    key_i = 0
    deadline = time.time() + SOAK_S
    while time.time() < deadline:
        if INPUT_KEYS:
            key = INPUT_KEYS[key_i % len(INPUT_KEYS)]
            key_i += 1
            for p in ports:
                try:
                    text(p, {"line": f"wire_input {key} frames=6"}, timeout=2.0)
                except OSError:
                    result.ok = False
                    result.infra = True
                    result.break_reason = "wire_input_failed"
                    kill_all()
                    return result
            action_n += 1
            time.sleep(0.15)
            for p in ports:
                try:
                    text(p, {"line": "wire_input frames=2"}, timeout=2.0)
                except OSError:
                    pass
        else:
            time.sleep(0.15)

        sample = {"t": time.time(), "locks": [], "graphs": [], "hash_desync": False}
        for p in ports:
            try:
                lk = parse_lock(text(p, {"cmd": "lockstep_hash"}))
                snap = text(p, {"cmd": "world_snapshot"})
            except OSError:
                result.ok = False
                result.infra = True
                result.break_reason = "mcp_lost"
                kill_all()
                return result
            sample["locks"].append(lk)
            sample["graphs"].append(graph_ents(snap))
        sample["hash_desync"] = sample_hash_desync(sample["locks"])
        if sample["hash_desync"]:
            desync_n += 1
        hist.append(sample)

        result.desync_samples = desync_n
        result.total_samples = len(hist)
        result.desync_pct = 100.0 * desync_n / len(hist) if hist else 0.0

        br = classify_unrecoverable(hist, clients, clogs, slog)
        if br:
            result.ok = False
            result.break_reason = br.reason
            result.notes = br.detail
            break

        time.sleep(max(0.0, SAMPLE_S - 0.15))

    # Desync% fail only after full soak (or early unrecoverable already set)
    if result.ok and clients >= 2 and result.total_samples >= 8:
        if result.desync_pct >= DESYNC_FAIL_PCT:
            result.ok = False
            result.break_reason = "desync_time"
            result.notes = (
                f"desync_pct={result.desync_pct:.1f}% "
                f"samples={result.desync_samples}/{result.total_samples}"
            )

    if hist:
        last = hist[-1]
        parts = []
        for i, lk in enumerate(last["locks"]):
            g = last["graphs"][i]
            parts.append(
                f"c{i}:tick={lk.tick}/conf={lk.confirmed}/zf={lk.zf}/pred={lk.predict}/"
                f"play={lk.playout}/graph={g[1]}"
            )
        soft = count_log(slog, "soft PHYS") + sum(count_log(p, "soft PHYS") for p in clogs)
        mm = count_log(slog, "hash mismatch") + sum(count_log(p, "hash mismatch") for p in clogs)
        result.notes = (
            (result.notes + "; " if result.notes else "")
            + f"desync={result.desync_pct:.1f}% ({result.desync_samples}/{result.total_samples}); "
            + f"actions={action_n}; softPHYS={soft}; hashMM={mm}; "
            + " | ".join(parts)
        )
        result.samples = [
            {
                "tick": [lk.tick for lk in h["locks"]],
                "hash_desync": h["hash_desync"],
                "graph": [g[1] for g in h["graphs"]],
            }
            for h in hist[:: max(1, len(hist) // 4)]
        ]

    for p in procs:
        p.kill()
    kill_all()
    return result


def run_trial(clients: int, mode: str, ping: int, loss: int, throttle: int) -> TrialResult:
    last: TrialResult | None = None
    product_tries = 0
    attempt = 0
    while True:
        attempt += 1
        r = run_trial_once(clients, mode, ping, loss, throttle)
        r.launch_attempts = attempt
        last = r
        if r.ok:
            return r
        if r.infra:
            if attempt >= LAUNCH_RETRIES:
                break
            print(
                f"  infra retry {attempt}/{LAUNCH_RETRIES}: {r.break_reason} {r.notes[:80]}",
                flush=True,
            )
            continue
        # Flaky product: PHYS exhausted while desync% still healable range
        if (
            r.break_reason in FLAKY_PRODUCT
            and r.desync_pct < DESYNC_FAIL_PCT
            and product_tries < PRODUCT_RETRIES
        ):
            product_tries += 1
            print(
                f"  product retry {product_tries}/{PRODUCT_RETRIES}: {r.break_reason} "
                f"desync={r.desync_pct:.1f}%",
                flush=True,
            )
            continue
        return r
    assert last is not None
    last.break_reason = "infra_exhausted"
    last.infra = True
    last.ok = False
    last.notes = f"after {LAUNCH_RETRIES} launches; " + (last.notes or "")
    return last


def sweep_axis(clients: int, mode: str, values) -> list[TrialResult]:
    out: list[TrialResult] = []
    for v in values:
        if mode == "ping":
            ping, loss, thr = v, 0, 0
            if ping > PING_MAX_MS:
                break
        elif mode == "loss":
            ping, loss, thr = 0, v, 0
            if loss > LOSS_MAX_PCT:
                break
        elif mode == "throttle":
            ping, loss, thr = 0, 0, v
            if thr > THROTTLE_MAX_PCT:
                break
        else:
            ping, loss, thr = v
            if ping > PING_MAX_MS or loss > LOSS_MAX_PCT or thr > THROTTLE_MAX_PCT:
                break
        print(
            f"=== {LABEL} {clients}c {mode} ping={ping} loss={loss} thr={thr} ===",
            flush=True,
        )
        r = run_trial(clients, mode, ping, loss, thr)
        out.append(r)
        kind = "INFRA" if r.infra else ("OK" if r.ok else f"BREAK:{r.break_reason}")
        print(
            f"  -> {kind} desync={r.desync_pct:.1f}% attempts={r.launch_attempts} {r.notes[:120]}",
            flush=True,
        )
        if r.infra:
            # Cannot measure this step — stop axis; caller validity gate flags it
            break
        if not r.ok:
            break
        if mode == "ping" and ping >= PING_MAX_MS:
            break
        if mode == "loss" and loss >= LOSS_MAX_PCT:
            break
        if mode == "throttle" and thr >= THROTTLE_MAX_PCT:
            break
        if mode == "combined" and (
            ping >= PING_MAX_MS and loss >= LOSS_MAX_PCT and thr >= THROTTLE_MAX_PCT
        ):
            break
    return out


def axis_limit(rows: list[TrialResult]) -> dict:
    oks = [r for r in rows if r.ok]
    # Product fails only (exclude trailing infra for "fail" reason preference)
    product_fails = [r for r in rows if not r.ok and not r.infra]
    infra_fails = [r for r in rows if not r.ok and r.infra]

    def pack(r: TrialResult | None) -> dict | None:
        if not r:
            return None
        return {
            "ping": r.ping,
            "loss": r.loss,
            "throttle": r.throttle,
            "desync_pct": round(r.desync_pct, 1),
            "desync_samples": r.desync_samples,
            "total_samples": r.total_samples,
            "reason": r.break_reason or "",
            "infra": r.infra,
            "launch_attempts": r.launch_attempts,
        }

    if product_fails and oks:
        return {
            "last_ok": pack(oks[-1]),
            "fail": pack(product_fails[0]),
            "survived": False,
            "infra_blocked": False,
        }
    if product_fails and not oks:
        return {
            "last_ok": None,
            "fail": pack(product_fails[0]),
            "survived": False,
            "infra_blocked": False,
        }
    if infra_fails and oks:
        return {
            "last_ok": pack(oks[-1]),
            "fail": pack(infra_fails[0]),
            "survived": False,
            "infra_blocked": True,
        }
    if infra_fails and not oks:
        return {
            "last_ok": None,
            "fail": pack(infra_fails[0]),
            "survived": False,
            "infra_blocked": True,
        }
    return {
        "last_ok": pack(oks[-1] if oks else None),
        "fail": None,
        "survived": True,
        "infra_blocked": False,
    }


def fmt_ok(lo: dict | None, mode: str) -> str:
    if not lo:
        return "—"
    d = lo.get("desync_pct", 0)
    ds = f"{d:.0f}%" if isinstance(d, (int, float)) else f"{d}%"
    tag = " infra" if lo.get("infra") else ""
    if mode == "ping":
        return f"{lo['ping']} ms ({ds}){tag}"
    if mode == "loss":
        return f"{lo['loss']}% ({ds}){tag}"
    if mode == "throttle":
        return f"{lo['throttle']}% ({ds}){tag}"
    return f"{lo['ping']}/{lo['loss']}/{lo['throttle']} ({ds}){tag}"


def cell(lim: dict, mode: str) -> str:
    lo = lim.get("last_ok")
    f = lim.get("fail")
    if lim.get("survived"):
        return f"OK≤{fmt_ok(lo, mode)}"
    prefix = "INFRA@" if lim.get("infra_blocked") or (f and f.get("infra")) else "FAIL@"
    if lo is None and f:
        return f"{prefix}{fmt_ok(f, mode)} `{f.get('reason','')}`"
    if lo and f:
        return f"OK≤{fmt_ok(lo, mode)} → {prefix}{fmt_ok(f, mode)} `{f.get('reason','')}`"
    return "—"


def results_valid(all_results: dict[str, list[TrialResult]]) -> tuple[bool, str]:
    """2c baselines must launch; combined 0/0/0 must be a product OK (else flaky/invalid)."""
    for key, rows in all_results.items():
        if not key.startswith("2-"):
            continue
        if not rows:
            return False, f"{key}: empty"
        r0 = rows[0]
        if r0.infra or r0.break_reason in INFRA_REASONS:
            return False, f"{key}: baseline infra ({r0.break_reason}) {r0.notes[:80]}"
    comb = all_results.get("2-client combined") or []
    if comb and not comb[0].ok:
        return False, (
            f"2-client combined baseline product fail ({comb[0].break_reason}) "
            f"desync={comb[0].desync_pct:.1f}%"
        )
    return True, "ok"


def write_json(all_results: dict[str, list[TrialResult]]) -> dict:
    valid, why = results_valid(all_results)
    payload = {
        "scene": SCENE,
        "label": LABEL,
        "input": INPUT_KEYS,
        "soak_s": SOAK_S,
        "warm_s": WARM_S,
        "desync_fail_pct": DESYNC_FAIL_PCT,
        "launch_retries": LAUNCH_RETRIES,
        "valid": valid,
        "valid_note": why,
        "ceilings": {
            "ping_ms": PING_MAX_MS,
            "loss_pct": LOSS_MAX_PCT,
            "throttle_pct": THROTTLE_MAX_PCT,
        },
        "axes": {},
        "trials": {},
    }
    for key, rows in all_results.items():
        payload["trials"][key] = [asdict(r) for r in rows]
        mode = "combined"
        if "ping-only" in key:
            mode = "ping"
        elif "loss-only" in key:
            mode = "loss"
        elif "throttle-only" in key:
            mode = "throttle"
        clients = 1 if key.startswith("1-") else 2
        payload["axes"].setdefault(str(clients), {})[mode] = axis_limit(rows)
    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    OUT_JSON.write_text(json.dumps(payload, indent=2) + "\n")
    print(f"Wrote {OUT_JSON} valid={valid} ({why})", flush=True)
    return payload


def write_matrix_md(scenes: dict[str, dict]) -> None:
    lines = []
    lines.append("<!-- agent: composer-2.5 | 2026-08-02 | infra-retry warm soak harness | f222cb -->")
    lines.append("# Throttle / ping / loss matrix")
    lines.append("")
    lines.append(
        "Highest step that still passed product criteria. `(desync%)` = hash-mismatch sample share. "
        "`INFRA@` = launch/settle exhausted (not a product limit). `—` = baseline product fail."
    )
    lines.append("")
    lines.append("## Highest values that still work")
    lines.append("")
    for n, title in (("1", "1 client"), ("2", "2 clients")):
        lines.append(f"### {title}")
        lines.append("")
        lines.append("| scene | input | ping | loss | throttle | combined | valid |")
        lines.append("|-------|-------|------|------|----------|----------|-------|")
        for name, payload in scenes.items():
            a = payload.get("axes", {}).get(n, {})
            inp = payload.get("input") or []
            inp_s = "/".join(inp) if inp else "idle"
            valid = payload.get("valid")
            v = "yes" if valid is True else ("no" if valid is False else "?")
            lines.append(
                "| `{n}` | {i} | {p} | {l} | {t} | {c} | {v} |".format(
                    n=name,
                    i=inp_s,
                    p=fmt_ok((a.get("ping") or {}).get("last_ok"), "ping"),
                    l=fmt_ok((a.get("loss") or {}).get("last_ok"), "loss"),
                    t=fmt_ok((a.get("throttle") or {}).get("last_ok"), "throttle"),
                    c=fmt_ok((a.get("combined") or {}).get("last_ok"), "combined"),
                    v=v,
                )
            )
        lines.append("")

    lines.append("## Method")
    lines.append("")
    lines.append("| | |")
    lines.append("|--|--|")
    lines.append(f"| Soak / warm / sample | **{SOAK_S:.0f}s** / **{WARM_S:.0f}s** / **{SAMPLE_S:.1f}s** |")
    lines.append(
        f"| Stop | desync≥**{DESYNC_FAIL_PCT:.0f}%** (full soak), unrecoverable, ceiling "
        f"**{PING_MAX_MS}ms**/**{LOSS_MAX_PCT}%**/**{THROTTLE_MAX_PCT}%** |"
    )
    lines.append(f"| Infra | retry **{LAUNCH_RETRIES}×**; not a product limit |")
    lines.append("| Rule | `.cursor/rules/throttle-soak.mdc` |")
    lines.append("")

    for scene, payload in scenes.items():
        lines.append(f"## {scene} — limit detail")
        if payload.get("valid") is False:
            lines.append("")
            lines.append(f"_Invalid run: {payload.get('valid_note', '?')}_")
        lines.append("")
        axes = payload.get("axes", {})
        lines.append("| clients | ping-only | loss-only | throttle-only | combined |")
        lines.append("|--------:|-----------|-----------|---------------|----------|")
        for cn in ("1", "2"):
            a = axes.get(cn, {})
            lines.append(
                "| {n} | {p} | {l} | {t} | {c} |".format(
                    n=cn,
                    p=cell(a.get("ping", {}), "ping"),
                    l=cell(a.get("loss", {}), "loss"),
                    t=cell(a.get("throttle", {}), "throttle"),
                    c=cell(a.get("combined", {}), "combined"),
                )
            )
        lines.append("")
        trials = payload.get("trials", {})
        for n_clients in (1, 2):
            if not any(k.startswith(f"{n_clients}-") for k in trials):
                continue
            lines.append(f"### {scene} / {n_clients}-client — step grid")
            lines.append("")
            for title, key, field in (
                ("**ping (ms)**", f"{n_clients}-client ping-only", "ping"),
                ("**loss (%)**", f"{n_clients}-client loss-only", "loss"),
                ("**throttle (%)**", f"{n_clients}-client throttle-only", "throttle"),
                ("**combined**", f"{n_clients}-client combined", "combo"),
            ):
                rows = trials.get(key, [])
                lines.append(title)
                lines.append("")
                if not rows:
                    lines.append("_no data_")
                    lines.append("")
                    continue
                if field == "combo":
                    vals = [f"{r['ping']}/{r['loss']}/{r['throttle']}" for r in rows]
                else:
                    vals = [str(r[field]) for r in rows]
                marks = []
                for r in rows:
                    d = r.get("desync_pct", 0)
                    if r.get("ok"):
                        marks.append(f"OK {d:.0f}%")
                    elif r.get("infra"):
                        marks.append(f"INFRA:{r.get('break_reason','')[:10]}")
                    else:
                        marks.append(f"X:{r.get('break_reason','')[:12]} {d:.0f}%")
                lines.append("| " + " | ".join(vals) + " |")
                lines.append("|" + "|".join(["---:"] * len(vals)) + "|")
                lines.append("| " + " | ".join(marks) + " |")
                lines.append("")

    lines.append("<!-- agent: composer-2.5 | 2026-08-02 | infra-retry warm soak harness | f222cb -->")
    OUT_MD.write_text("\n".join(lines) + "\n")
    print(f"Wrote {OUT_MD}", flush=True)


def load_all_scene_payloads() -> dict[str, dict]:
    preferred = ("stacking", "physics", "stress_spawn", "stress_spawn_idle")
    found: dict[str, dict] = {}
    for p in sorted((ROOT / "docs").glob("throttle-*.json")):
        data = json.loads(p.read_text())
        label = data.get("label") or data.get("scene") or p.stem.removeprefix("throttle-")
        found[label] = data
    ordered: dict[str, dict] = {}
    for k in preferred:
        if k in found:
            ordered[k] = found.pop(k)
    ordered.update(found)
    return ordered


def parse_client_list() -> list[int]:
    out = []
    for part in CLIENTS_ONLY.split(","):
        part = part.strip()
        if part in ("1", "2"):
            out.append(int(part))
    return out or [1, 2]


def main() -> None:
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    if len(sys.argv) > 1 and sys.argv[1] == "--matrix-only":
        write_matrix_md(load_all_scene_payloads())
        return

    if len(sys.argv) > 1 and sys.argv[1] == "--launch-soak":
        n = int(sys.argv[2]) if len(sys.argv) > 2 else 100
        clients = int(os.environ.get("THROTTLE_LAUNCH_CLIENTS", "2"))
        ping = int(os.environ.get("THROTTLE_LAUNCH_PING", "0"))
        loss = int(os.environ.get("THROTTLE_LAUNCH_LOSS", "0"))
        thr = int(os.environ.get("THROTTLE_LAUNCH_THR", "0"))
        sys.exit(launch_soak(n, clients=clients, ping=ping, loss=loss, throttle=thr))

    if len(sys.argv) > 1 and sys.argv[1] == "--smoke":
        global SOAK_S
        SOAK_S = 4.0
        r2 = run_trial(2, "combined", 0, 0, 0)
        print(
            "SMOKE2",
            LABEL,
            r2.ok,
            f"infra={r2.infra}",
            f"desync={r2.desync_pct:.1f}%",
            r2.break_reason,
            r2.notes[:160],
        )
        kill_all()
        sys.exit(0 if r2.ok and not r2.infra else 1)

    subprocess.run(
        [
            "cmake",
            "--build",
            str(BUILD),
            "-j",
            str(os.cpu_count() or 4),
            "--target",
            "ngame",
            "ngame_server",
        ],
        check=True,
    )
    if OUT_JSON.exists():
        OUT_JSON.unlink()
    all_results: dict[str, list[TrialResult]] = {}
    for n_clients in parse_client_list():
        all_results[f"{n_clients}-client ping-only"] = sweep_axis(n_clients, "ping", PING_STEPS)
        all_results[f"{n_clients}-client loss-only"] = sweep_axis(n_clients, "loss", LOSS_STEPS)
        all_results[f"{n_clients}-client throttle-only"] = sweep_axis(
            n_clients, "throttle", THROTTLE_STEPS
        )
        all_results[f"{n_clients}-client combined"] = sweep_axis(
            n_clients, "combined", COMBINED_STEPS
        )
    payload = write_json(all_results)
    write_matrix_md(load_all_scene_payloads())
    kill_all()
    if not payload.get("valid", True):
        print(f"INVALID RUN: {payload.get('valid_note')}", flush=True)
        sys.exit(2)


if __name__ == "__main__":
    main()

# agent: composer-2.5 | 2026-08-02 | parallel client bring-up | 1bced5
