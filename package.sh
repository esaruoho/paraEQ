#!/usr/bin/env bash
# Build ParaEQ 301 and package a distributable zip for direct sale (Gumroad).
# Produces: dist/ParaEQ-301-macOS-YYYYMMDD-HHMMSS.zip  (AU + VST3 + Standalone, dequarantined)
# Also writes the produced path to dist/LATEST_ZIP.txt so Convey / Gumroad tooling
# can reference "the build Convey just made" without guessing the timestamp.
#
# Usage:
#   ./package.sh            # build (via start.sh) then zip
#   SKIP_BUILD=1 ./package.sh   # zip existing Release artefacts only
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

REL="$ROOT/build/ParaEQ301_artefacts/Release"
AU="$REL/AU/ParaEQ 301.component"
VST3="$REL/VST3/ParaEQ 301.vst3"
APP="$REL/Standalone/ParaEQ 301.app"
DIST="$ROOT/dist"
TS="$(date +%Y%m%d-%H%M%S)"
ZIP="$DIST/ParaEQ-301-macOS-$TS.zip"

if [[ "${SKIP_BUILD:-}" != "1" ]]; then
  echo "package.sh: (1/4) build"
  "$ROOT/start.sh"
else
  echo "package.sh: (1/4) build skipped (SKIP_BUILD=1)"
fi

echo "package.sh: (2/4) verify artefacts"
for b in "$AU" "$VST3" "$APP"; do
  if [[ ! -d "$b" ]]; then
    echo "package.sh: missing artefact: $b" >&2
    exit 1
  fi
done

echo "package.sh: (3/4) dequarantine"
xattr -dr com.apple.quarantine "$AU" "$VST3" "$APP" 2>/dev/null || true

echo "package.sh: (4/4) zip → $ZIP"
mkdir -p "$DIST"
rm -f "$ZIP"
# Stage with clean top-level names (AU/ VST3/ Standalone/) and no .DS_Store noise.
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/AU" "$STAGE/VST3" "$STAGE/Standalone"
cp -R "$AU"   "$STAGE/AU/"
cp -R "$VST3" "$STAGE/VST3/"
cp -R "$APP"  "$STAGE/Standalone/"
( cd "$STAGE" && find . -name '.DS_Store' -delete && zip -r -X -q "$ZIP" AU VST3 Standalone )

printf '%s\n' "$ZIP" > "$DIST/LATEST_ZIP.txt"

SIZE="$(du -h "$ZIP" | cut -f1)"
echo ""
echo "package.sh: done."
echo "  zip:  $ZIP  ($SIZE)"
echo "  ref:  $DIST/LATEST_ZIP.txt → $(cat "$DIST/LATEST_ZIP.txt")"
echo "  (contents: AU/ParaEQ 301.component, VST3/ParaEQ 301.vst3, Standalone/ParaEQ 301.app)"
