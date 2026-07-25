# ngame

Headless server + raylib client. ENet (native), WebSocket (web), agent TCP (tools/MCP).

## Build

```bash
cmake -B build -S .
cmake --build build -j$(nproc)
```

Requires [raylib](https://www.raylib.com/) at `RAYLIB_DIR` in `CMakeLists.txt` (or edit that path).

## Run

```bash
killall -9 ngame_server ng_test_net ngame 2>/dev/null || true
./build/ngame_server          # wait for: NG: server ready
./build/ngame --connect 127.0.0.1:27015
```

Tab → console → `scene cube` | `scene sphere`

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

If connect fails: ensure no zombie server (`ss -ulnp | grep 27015`), restart `ngame_server`, wait for `server ready`.
