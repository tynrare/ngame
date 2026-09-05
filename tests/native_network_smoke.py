"""Exercise native TCP, WebSocket, and ENet on a server owned by this test."""
import base64
import hashlib
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time


def request(port, payload):
    with socket.create_connection(("127.0.0.1", port), timeout=3) as connection:
        connection.sendall(json.dumps(payload).encode() + b"\n")
        with connection.makefile("rb") as stream:
            return json.loads(stream.readline())


def main():
    server, client = map(lambda s: str(Path(s).resolve()), sys.argv[1:3])
    # The current server exposes fixed TCP ports. Refuse to touch an existing game.
    reservations = []
    try:
        for port, kind in [(27015, socket.SOCK_DGRAM), (27016, socket.SOCK_STREAM), (27100, socket.SOCK_STREAM)]:
            sock = socket.socket(socket.AF_INET, kind)
            reservations.append(sock)
            sock.bind(("127.0.0.1", port))
    finally:
        for sock in reservations:
            sock.close()

    with tempfile.TemporaryFile() as log:
        process = subprocess.Popen([server], cwd=Path(server).parent, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 10
            while True:
                if process.poll() is not None:
                    raise RuntimeError(f"Server exited with {process.returncode}")
                try:
                    snapshot = request(27100, {"cmd": "world_snapshot"})
                    break
                except (OSError, ValueError):
                    if time.monotonic() > deadline:
                        raise
                    time.sleep(0.05)
            assert snapshot.get("ok") and "scene=" in snapshot.get("text", ""), snapshot
            # Leave a client idle first: WSAEWOULDBLOCK must not disconnect it.
            with socket.create_connection(("127.0.0.1", 27100), timeout=3) as connection:
                time.sleep(0.1)
                connection.sendall(b'{"cmd":"world_snapshot"}\n')
                with connection.makefile("rb") as stream:
                    assert json.loads(stream.readline())["ok"]
            with socket.create_connection(("127.0.0.1", 27016), timeout=3) as connection:
                key = "dGhlIHNhbXBsZSBub25jZQ=="
                connection.sendall(("GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n"
                                    "Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
                                    f"Sec-WebSocket-Key: {key}\r\n\r\n").encode())
                response = b""
                while b"\r\n\r\n" not in response:
                    data = connection.recv(4096)
                    if not data:
                        raise RuntimeError("WebSocket closed before handshake")
                    response += data
                expected = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
                assert b"101 Switching Protocols" in response and expected in response, response
            result = subprocess.run([client, "127.0.0.1", "27015"], cwd=Path(server).parent,
                                    capture_output=True, text=True, timeout=20)
            assert result.returncode == 0, result.stdout + result.stderr
            assert "SESSION scene=cube" in result.stdout, result.stdout
            print("Native TCP idle/reconnect, WebSocket handshake, and ENet scene exchange passed")
        except BaseException:
            log.seek(0)
            print(log.read().decode(errors="replace"), file=sys.stderr)
            raise
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


if __name__ == "__main__":
    main()
