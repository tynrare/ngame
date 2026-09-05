# ngame
# [mgame](https://github.com/tynrare/mgame)

Headless server + raylib client. Three launch modes on native; embedded in-process on web.

## Build

Requires CMake 3.22+ and a C/C++ compiler. Windows uses MinGW-w64 (the
`w64devkit` included with the raylib Windows installer works).

Windows, from PowerShell in the repository root:

```powershell
cmake --preset windows -DRAYLIB_DIR=C:/raylib/raylib-6.0
cmake --build --preset windows --parallel
cmake --build --preset windows --target run
```

The preset finds `C:/raylib/w64devkit` automatically. For another installation,
pass `-DNG_MINGW_ROOT=D:/tools/w64devkit` on the first configure, or set the
`NG_MINGW_ROOT` environment variable. Compiler helper paths are supplied by the
toolchain; no special shell is required. Use a new build directory when changing
compilers. The Windows preset builds into `build/windows`.

Linux / other native toolchains:

```bash
cmake --preset native
cmake --build --preset native --parallel
cmake --build --preset native --target run
```

Plain CMake also works: `cmake -S . -B build`, followed by
`cmake --build build --parallel`. On Windows, use the `windows` preset above to
select MinGW instead of CMake's default Visual Studio generator.

Dependencies are configured without editing `CMakeLists.txt`:

| Setting | Default | Purpose |
|---------|---------|---------|
| `RAYLIB_DIR` | `$ENV{RAYLIB_DIR}`, otherwise empty | Local raylib **source** tree. Also accepts a Windows installer root containing `raylib/`. |
| `NG_FETCH_DEPENDENCIES` | `ON` | Download raylib when `RAYLIB_DIR` is empty; set `OFF` for an offline build. |
| `NG_RAYLIB_REVISION` | `6.0` | Release tag used for the automatic raylib download. |
| `BOX3D_DIR` | `third_party/box3d` | Bundled Box3D source tree; accepts a custom source path. |

Without a local raylib path, CMake downloads the pinned release into the build
tree on first configure (requires Git and network access). Subsequent builds
reuse it. Local sources are never updated or overwritten by CMake. To switch
back to downloaded raylib, pass `-DRAYLIB_DIR=`. Dependency examples are disabled
by default. Both libraries are built using the game's selected compiler and
configuration; raylib sources are required because the game also uses its
bundled `stb_truetype.h` and a custom texture-unit limit.

For paths containing spaces, quote the entire argument, e.g.
`"-DRAYLIB_DIR=D:/Game Libraries/raylib"`. CMake remembers these settings in the
build directory. Personal presets can go in the git-ignored
`CMakeUserPresets.json`.

The `run` target builds the client and server, then starts the game from the
binary directory so resources are found. On Windows the local server runs
without a console window and exits when its owning game closes.

## Launch modes

| Mode | Flag | Default | Description |
|------|------|---------|-------------|
| **Local** | `--local [--port PORT]` | native | Spawns `ngame_server` next to `ngame`, connects to `127.0.0.1:27015` |
| **Remote** | `--remote HOST:PORT` | — | Connect to an existing server |
| **Embedded** | `--embedded` | web | Client + server in one process via in-memory loopback (same wire protocol) |

Aliases: `--connect HOST:PORT` (same as `--remote`).

Sim / pacing (client and server):

| Flag | Unit | Effect |
|------|------|--------|
| `--ping MS` | milliseconds | one-way LOCK_INPUT latency (server; `--local` forwards to spawned server) |
| `--loss PCT` | percent 0..100 | drop unreliable LOCK_INPUT (server inbound / client uplink) |
| `--throttle PCT` | percent 0..100 | drop FPS from 60Hz baseline (50 → ~30Hz) |

Examples:

```bash
./build/ngame --local                    # explicit local spawn
./build/ngame --remote 127.0.0.1:27015 # remote
./build/ngame --embedded               # in-process (native)
./build/ngame --local --ping 80 --loss 10 --throttle 50
./build/ngame_server --ping 80 --loss 10 --throttle 25
./build/ngame --help
```

Manual server (remote / tools):

```bash
killall -9 ngame_server ng_test_net ngame 2>/dev/null || true
./build/ngame_server
./build/ngame --connect 127.0.0.1:27015
```

Tab → console → `scene <id>` (`cube`, `sphere`, `example`). Server runs `res/boot.js` on start.

<!-- agent: composer-2.5 | 2026-07-29 | add agent runbook refs | 3e2af1 -->
Docs: [architecture](docs/architecture.md) · [scenes](docs/scenes.md)
Agent workflow: [agent runbook](docs/agent-runbook.md)

## Test

Portable smoke checks (Windows preset shown):

```powershell
cmake --build --preset windows --target smoke --parallel
ctest --preset windows
```

These run JavaScript scene loading and physics save/restore checks, plus native
TCP/WebSocket/ENet checks when Python 3 is available. Close the game first:
the network check requires ports 27015, 27016, and 27100 to be free and only
stops the server it starts. Use `native` in place of `windows` for Linux.
The longer validation/soak suite below requires Linux shell utilities:

```bash
./scripts/validate.sh
./build/ng_test_net 127.0.0.1 27015
```

For agent-driven changes, use `docs/agent-runbook.md` as the verification checklist.

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
<!-- agent: composer-2.5 | 2026-07-29 | add agent runbook refs | 3e2af1 -->
