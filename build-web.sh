#!/usr/bin/env bash
# agent: composer-2.5 | 2026-07-25 | emscripten web client build | c3d91f
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
EMSDK="${EMSDK:-/home/x/emsdk}"
OUT="$ROOT/build-web"

if [[ ! -f "$EMSDK/emsdk_env.sh" ]]; then
  echo "emsdk not found at $EMSDK" >&2
  exit 1
fi
# shellcheck disable=SC1091
source "$EMSDK/emsdk_env.sh"

rm -rf "$OUT"
mkdir -p "$OUT"
cd "$OUT"
emcmake cmake "$ROOT" -DPLATFORM=Web
emmake make ngame -j"$(nproc)"
test -f "$OUT/ngame.html"
echo "web build ok: $OUT/ngame.html"
