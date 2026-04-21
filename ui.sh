#!/usr/bin/env bash
# Rebuild only the Standalone app and open it — fastest loop for editor / layout tweaks (no DAW).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
if [[ ! -f build/build.ninja ]]; then
  echo "ui.sh: run ./start.sh once (or: mkdir -p build && cd build && cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release)" >&2
  exit 1
fi
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 8)"
# Re-run configure so CMakeLists.txt can strip stale -L/usr/local/opt/zlib/lib from cached linker flags (ld warning).
cmake -S "$ROOT" -B "$ROOT/build" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$ROOT/build" --parallel "$JOBS" --target ParaEQ301_Standalone
open "$ROOT/build/ParaEQ301_artefacts/Release/Standalone/ParaEQ 301.app"
