#!/usr/bin/env python3
# agent: composer-2.5 | 2026-07-25 | websocket smoke client | e5f93b
# agent: composer-2.5 | 2026-07-25 | ws ACTION_RESULT decode | c4d5e6
"""Send scene cube over WebSocket; print action result reply text."""

import socket
import struct
import sys

HOST = "127.0.0.1"
PORT = 27016
NG_PROTO_MAGIC = 0x4E474D45
NG_PKT_CMD = 3
NG_PKT_CMD_REPLY = 4
NG_PKT_ACTION_RESULT = 6


def ws_handshake(sock: socket.socket, host: str, port: int) -> None:
    key = "dGhlIHNhbXBsZSBub25jZQ=="
    req = (
        f"GET / HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        f"Upgrade: websocket\r\n"
        f"Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        f"Sec-WebSocket-Version: 13\r\n\r\n"
    )
    sock.sendall(req.encode())
    resp = b""
    while b"\r\n\r\n" not in resp:
        chunk = sock.recv(4096)
        if not chunk:
            break
        resp += chunk
    if b"101" not in resp:
        raise RuntimeError(f"ws handshake failed: {resp[:160]!r}")


def ws_close(sock: socket.socket) -> None:
    sock.sendall(b"\x88\x00")


def encode_cmd(line: str) -> bytes:
    line_b = line.encode()[:255]
    body = bytes([len(line_b)]) + line_b
    hdr = struct.pack("<IBBBBHI", NG_PROTO_MAGIC, 1, 1, NG_PKT_CMD, 0, 1, 0)
    return hdr + body


def decode_action_result(payload: bytes) -> str | None:
    if len(payload) < 16:
        return None
    magic, = struct.unpack_from("<I", payload, 0)
    if magic != NG_PROTO_MAGIC:
        return None
    ptype = payload[6]
    pos = 14
    if ptype == NG_PKT_CMD_REPLY:
        (n,) = struct.unpack_from("<H", payload, pos)
        pos += 2
        if pos + n > len(payload):
            return None
        return payload[pos : pos + n].decode(errors="replace")
    if ptype != NG_PKT_ACTION_RESULT:
        return None
    pos += 4  # state_hash
    pos += 1  # kind
    (reply_len,) = struct.unpack_from("<H", payload, pos)
    pos += 2
    if pos + reply_len > len(payload):
        return None
    return payload[pos : pos + reply_len].decode(errors="replace")


def recv_frames(sock: socket.socket) -> str | None:
    sock.settimeout(2.0)
    for _ in range(60):
        try:
            hdr = sock.recv(2)
        except TimeoutError:
            break
        if len(hdr) < 2:
            break
        plen = hdr[1] & 0x7F
        if plen == 126:
            plen = struct.unpack(">H", sock.recv(2))[0]
        payload = b""
        while len(payload) < plen:
            chunk = sock.recv(plen - len(payload))
            if not chunk:
                break
            payload += chunk
        text = decode_action_result(payload)
        if text and ("cube" in text or "scene loaded" in text):
            return text
    return None


def main() -> int:
    host = sys.argv[1] if len(sys.argv) > 1 else HOST
    port = int(sys.argv[2]) if len(sys.argv) > 2 else PORT
    pkt = encode_cmd("scene cube")
    frame = b"\x82" + bytes([len(pkt)]) + pkt
    sock = socket.create_connection((host, port), timeout=5)
    try:
        ws_handshake(sock, host, port)
        sock.sendall(frame)
        text = recv_frames(sock)
        if text:
            print(text)
        ws_close(sock)
        return 0 if text and "cube" in text else 1
    finally:
        sock.close()


if __name__ == "__main__":
    raise SystemExit(main())
