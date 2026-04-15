#!/usr/bin/env bash
# Build ParaEQ 301, install AU + VST3 into ~/Library/Audio/Plug-Ins/, strip Gatekeeper quarantine.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

echo "deploy.sh: (1/3) build"
"$ROOT/start.sh"

AU_NAME="ParaEQ 301.component"
VST3_NAME="ParaEQ 301.vst3"
AU_SRC="$ROOT/build/ParaEQ301_artefacts/Release/AU/$AU_NAME"
VST3_SRC="$ROOT/build/ParaEQ301_artefacts/Release/VST3/$VST3_NAME"

if [[ ! -d "$AU_SRC" ]]; then
  echo "deploy.sh: missing AU bundle: $AU_SRC" >&2
  exit 1
fi
if [[ ! -d "$VST3_SRC" ]]; then
  echo "deploy.sh: missing VST3 bundle: $VST3_SRC" >&2
  exit 1
fi

AU_DST="$HOME/Library/Audio/Plug-Ins/Components"
VST3_DST="$HOME/Library/Audio/Plug-Ins/VST3"

echo "deploy.sh: (2/3) copy → $AU_DST and $VST3_DST"
mkdir -p "$AU_DST" "$VST3_DST"
rm -rf "$AU_DST/$AU_NAME" "$VST3_DST/$VST3_NAME"
cp -R "$AU_SRC" "$AU_DST/"
cp -R "$VST3_SRC" "$VST3_DST/"

echo "deploy.sh: (3/3) dequarantine (xattr -dr com.apple.quarantine …)"
xattr -dr com.apple.quarantine "$AU_DST/$AU_NAME"
xattr -dr com.apple.quarantine "$VST3_DST/$VST3_NAME"

echo ""
echo "deploy.sh: done."
echo "  AU:   $AU_DST/$AU_NAME"
echo "  VST3: $VST3_DST/$VST3_NAME"
