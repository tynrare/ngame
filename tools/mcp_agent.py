#!/usr/bin/env python3
# agent: composer-2.5 | 2026-07-25 | MCP stdio agent bridge | d4e82a
# agent: composer-2.5 | 2026-07-28 | mcp dual port render tool | 13f975
"""Minimal MCP stdio server forwarding tool calls to ngame gateway or ngame_server agent TCP."""

from __future__ import annotations

import json
import os
import socket
import sys
from typing import Any

AGENT_HOST = "127.0.0.1"
AGENT_PORTS = tuple(
    p
    for p in (
        int(os.environ.get("NG_AGENT_PORT", "0")),
        *range(27101, 27110),
        27100,
    )
    if p > 0
)
PROTOCOL_VERSION = "2024-11-05"

TOOLS = [
    {
        "name": "world_snapshot",
        "description": "Query local server scene plus render-side state.",
        "inputSchema": {"type": "object", "properties": {}, "required": []},
    },
    {
        "name": "render_snapshot",
        "description": "Query render-side scene label, snapshot entities, and graph count.",
        "inputSchema": {"type": "object", "properties": {}, "required": []},
    },
    {
        "name": "entity_transforms",
        "description": "Observe view-graph entity id/desc/pos/rot/scale.",
        "inputSchema": {"type": "object", "properties": {}, "required": []},
    },
    {
        "name": "wire_input",
        "description": "Inject A/D/W/S input for N frames (client/gateway only).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "keys": {
                    "type": "string",
                    "description": "Keys to hold, e.g. 'A', 'AD', 'W S'.",
                },
                "frames": {"type": "integer", "description": "Hold duration in frames."},
            },
            "required": ["keys"],
        },
    },
    {
        "name": "wire_mouse",
        "description": "Inject mouse screen position for N frames (drives raycast_plane_y).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "x": {"type": "number"},
                "y": {"type": "number"},
                "frames": {"type": "integer"},
            },
            "required": ["x", "y"],
        },
    },
    {
        "name": "raycast_plane_y",
        "description": "Observe current mouse ray hit on a Y plane.",
        "inputSchema": {
            "type": "object",
            "properties": {"y": {"type": "number"}},
            "required": [],
        },
    },
    {
        "name": "scene",
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
    last_err: Exception | None = None
    ports = [p for p in AGENT_PORTS if p > 0]
    if not ports:
        ports = list(range(27101, 27110)) + [27100]
    for port in ports:
        try:
            with socket.create_connection((AGENT_HOST, port), timeout=10) as sock:
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
        except OSError as exc:
            last_err = exc
            continue
    raise OSError(str(last_err) if last_err else "agent connect failed")


def run_tool(name: str, arguments: dict[str, Any]) -> str:
    # agent: composer-2.5 | 2026-07-29 | mcp wire input transform tools | 2f9c8a
    if name == "world_snapshot":
        resp = agent_request({"cmd": "world_snapshot"})
    elif name == "render_snapshot":
        resp = agent_request({"cmd": "render_snapshot"})
    elif name == "entity_transforms":
        resp = agent_request({"cmd": "entity_transforms"})
    elif name == "wire_input":
        keys = str(arguments.get("keys", "")).strip()
        frames = int(arguments.get("frames", 30) or 30)
        resp = agent_request({"line": f"wire_input {keys} frames={frames}"})
    elif name == "wire_mouse":
        x = float(arguments.get("x", 0))
        y = float(arguments.get("y", 0))
        frames = int(arguments.get("frames", 30) or 30)
        resp = agent_request({"line": f"wire_mouse {x} {y} frames={frames}"})
    elif name == "raycast_plane_y":
        y = float(arguments.get("y", 0) or 0)
        resp = agent_request({"line": f"raycast_plane_y {y}"})
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
                    "serverInfo": {"name": "ngame-agent", "version": "0.2.0"},
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

# agent: composer-2.5 | 2026-07-28 | mcp dual port render tool | 13f975
# agent: composer-2.5 | 2026-07-29 | mcp wire input transform tools | 2f9c8a
