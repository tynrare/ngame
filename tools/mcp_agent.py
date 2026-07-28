#!/usr/bin/env python3
# agent: composer-2.5 | 2026-07-25 | MCP stdio agent bridge | d4e82a
"""Minimal MCP stdio server forwarding tool calls to ngame_server agent TCP."""

from __future__ import annotations

import json
import socket
import sys
from typing import Any

AGENT_HOST = "127.0.0.1"
AGENT_PORT = 27100
PROTOCOL_VERSION = "2024-11-05"

TOOLS = [
    {
        "name": "world_snapshot",
        "description": "Query authoritative scene, entity count, and sim tick.",
        "inputSchema": {"type": "object", "properties": {}, "required": []},
    },
    {
        "name": "scene",
        # agent: composer-2.5 | 2026-07-28 | scene id any string | 3f5e2a
        "description": "Load a scene by id (e.g. sphere, cube, d-test).",
        "inputSchema": {
            "type": "object",
            "properties": {"id": {"type": "string"}},
            "required": ["id"],
        },
    },
    {
        "name": "command",
        "description": "Run a bus.js command line on the server.",
        "inputSchema": {
            "type": "object",
            "properties": {"line": {"type": "string"}},
            "required": ["line"],
        },
    },
]


def agent_request(payload: dict[str, Any]) -> dict[str, Any]:
    data = (json.dumps(payload, separators=(",", ":")) + "\n").encode()
    with socket.create_connection((AGENT_HOST, AGENT_PORT), timeout=10) as sock:
        sock.sendall(data)
        buf = b""
        while b"\n" not in buf:
            chunk = sock.recv(4096)
            if not chunk:
                break
            buf += chunk
    line = buf.split(b"\n", 1)[0].decode().strip()
    if not line:
        return {"ok": False, "error": "empty agent response"}
    return json.loads(line)


def run_tool(name: str, arguments: dict[str, Any]) -> str:
    if name == "world_snapshot":
        resp = agent_request({"cmd": "world_snapshot"})
    elif name == "scene":
        scene_id = arguments.get("id", "")
        resp = agent_request({"line": f"scene {scene_id}"})
    elif name == "command":
        line = arguments.get("line", "")
        resp = agent_request({"line": line})
    else:
        return json.dumps({"ok": False, "error": f"unknown tool: {name}"})

    if resp.get("ok"):
        return resp.get("text", json.dumps(resp))
    return json.dumps(resp)


def send(msg: dict[str, Any]) -> None:
    body = json.dumps(msg, separators=(",", ":")).encode()
    sys.stdout.buffer.write(f"Content-Length: {len(body)}\r\n\r\n".encode())
    sys.stdout.buffer.write(body)
    sys.stdout.buffer.flush()


def recv() -> dict[str, Any] | None:
    headers: dict[str, str] = {}
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None
        if line in (b"\r\n", b"\n"):
            break
        key, val = line.decode().split(":", 1)
        headers[key.strip().lower()] = val.strip()
    length = int(headers.get("content-length", "0"))
    if length <= 0:
        return None
    body = sys.stdin.buffer.read(length)
    return json.loads(body.decode())


def handle(msg: dict[str, Any]) -> None:
    mid = msg.get("id")
    method = msg.get("method", "")
    params = msg.get("params") or {}

    if method == "initialize":
        send(
            {
                "jsonrpc": "2.0",
                "id": mid,
                "result": {
                    "protocolVersion": PROTOCOL_VERSION,
                    "capabilities": {"tools": {}},
                    "serverInfo": {"name": "ngame-agent", "version": "0.1.0"},
                },
            }
        )
        return

    if method == "notifications/initialized":
        return

    if method == "tools/list":
        send({"jsonrpc": "2.0", "id": mid, "result": {"tools": TOOLS}})
        return

    if method == "tools/call":
        name = params.get("name", "")
        args = params.get("arguments") or {}
        try:
            text = run_tool(name, args)
            send(
                {
                    "jsonrpc": "2.0",
                    "id": mid,
                    "result": {"content": [{"type": "text", "text": text}]},
                }
            )
        except Exception as exc:  # noqa: BLE001
            send(
                {
                    "jsonrpc": "2.0",
                    "id": mid,
                    "result": {
                        "content": [{"type": "text", "text": str(exc)}],
                        "isError": True,
                    },
                }
            )
        return

    if mid is not None:
        send(
            {
                "jsonrpc": "2.0",
                "id": mid,
                "error": {"code": -32601, "message": f"method not found: {method}"},
            }
        )


def main() -> None:
    while True:
        msg = recv()
        if msg is None:
            break
        handle(msg)


if __name__ == "__main__":
    main()
