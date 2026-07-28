# ngame

Headless server + raylib client. Three launch modes on native; embedded in-process on web.

## Build

```bash
cmake -B build -S .
cmake --build build -j$(nproc)
```

Requires [raylib](https://www.raylib.com/) at `RAYLIB_DIR` in `CMakeLists.txt`.

## Launch modes

| Mode | Flag | Default | Description |
|------|------|---------|-------------|
| **Local** | `--local [--port PORT]` | native | Spawns `ngame_server` next to `ngame`, connects to `127.0.0.1:27015` |
| **Remote** | `--remote HOST:PORT` | — | Connect to an existing server |
| **Embedded** | `--embedded` | web | Client + server in one process via in-memory loopback (same wire protocol) |

Aliases: `--connect HOST:PORT` (same as `--remote`).

Examples:

```bash
./build/ngame --local                    # explicit local spawn
./build/ngame --remote 127.0.0.1:27015 # remote
./build/ngame --embedded               # in-process (native)
./build/ngame --help
```

Manual server (remote / tools):

```bash
killall -9 ngame_server ng_test_net ngame 2>/dev/null || true
./build/ngame_server
./build/ngame --connect 127.0.0.1:27015
```

Tab → console → `scene <id>` (`cube`, `sphere`, `example`). Server runs `res/boot.js` on start.

Docs: [architecture](docs/architecture.md) · [scenes](docs/scenes.md)

## Test

```bash
./scripts/validate.sh
./build/ng_test_net 127.0.0.1 27015
```

## Ports

| Service   | Port  |
|-----------|-------|
| ENet      | 27015 |
| WebSocket | 27016 |
| Agent TCP | 27100 |

## Network

- Snapshots + input: UDP (ENet channel 0, unreliable)
- CMD / REPLY / EVENT: reliable (channel 1)
- Embedded: loopback queues, same `ng_proto` packets, no sockets
