#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

if ! command -v cmake >/dev/null 2>&1; then
  echo "start.sh: cmake not found. Install CMake (e.g. brew install cmake)." >&2
  exit 1
fi

if ! command -v ninja >/dev/null 2>&1; then
  echo "start.sh: ninja not found. Install Ninja (e.g. brew install ninja)." >&2
  exit 1
fi

mkdir -p build
cd build

# Stale Intel Homebrew zlib path in LDFLAGS → ld: warning: search path '/usr/local/opt/zlib/lib' not found (common on arm64).
if [[ -n "${LDFLAGS:-}" ]]; then
    LDFLAGS="${LDFLAGS//-L\/usr\/local\/opt\/zlib\/lib/}"
    export LDFLAGS
fi

echo "start.sh: configuring (JUCE is fetched on first run, may take a minute)..."
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release

echo "start.sh: building AU + VST3 + Standalone…"
cmake --build . --config Release --parallel "$(sysctl -n hw.ncpu 2>/dev/null || echo 8)" --target ParaEQ301_All

echo ""
echo "start.sh: done."
echo "  AU:         $ROOT/build/ParaEQ301_artefacts/Release/AU/ParaEQ 301.component"
echo "  VST3:       $ROOT/build/ParaEQ301_artefacts/Release/VST3/ParaEQ 301.vst3"
echo "  Standalone: $ROOT/build/ParaEQ301_artefacts/Release/Standalone/ParaEQ 301.app  (quick UI: ./ui.sh)"
echo "  (COPY_PLUGIN_AFTER_BUILD may also install under ~/Library/Audio/Plug-Ins/)"
