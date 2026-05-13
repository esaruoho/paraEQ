#!/usr/bin/env bash
# Build ParaEQ 301, install AU + VST3 into ~/Library/Audio/Plug-Ins/, strip Gatekeeper quarantine,
# then commit any repo changes (if needed) and push to GitHub (origin).
#
# Usage:
#   ./deploy.sh                    # commit message: "Deploy: <UTC timestamp>"
#   ./deploy.sh "Release 1.0.1"   # custom commit message
#   DEPLOY_NO_PUSH=1 ./deploy.sh  # build + copy + quarantine only (no git)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

COMMIT_MSG="${1:-Deploy: $(date -u +"%Y-%m-%d %H:%M UTC")}"

echo "deploy.sh: (1/4) build"
"$ROOT/start.sh"

AU_NAME="ParaEQ 301.component"
VST3_NAME="ParaEQ 301.vst3"
STANDALONE_NAME="ParaEQ 301.app"
AU_SRC="$ROOT/build/ParaEQ301_artefacts/Release/AU/$AU_NAME"
VST3_SRC="$ROOT/build/ParaEQ301_artefacts/Release/VST3/$VST3_NAME"
STANDALONE_SRC="$ROOT/build/ParaEQ301_artefacts/Release/Standalone/$STANDALONE_NAME"

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

echo "deploy.sh: (2/4) copy → $AU_DST and $VST3_DST"
mkdir -p "$AU_DST" "$VST3_DST"
rm -rf "$AU_DST/$AU_NAME" "$VST3_DST/$VST3_NAME"
cp -R "$AU_SRC" "$AU_DST/"
cp -R "$VST3_SRC" "$VST3_DST/"

echo "deploy.sh: (3/4) dequarantine (xattr -dr com.apple.quarantine …)"
xattr -dr com.apple.quarantine "$AU_DST/$AU_NAME"
xattr -dr com.apple.quarantine "$VST3_DST/$VST3_NAME"

if [[ -d "$STANDALONE_SRC" ]]; then
  xattr -dr com.apple.quarantine "$STANDALONE_SRC" 2>/dev/null || true
  echo "deploy.sh: launching Standalone preview ($STANDALONE_SRC)"
  pkill -f "ParaEQ 301.app/Contents/MacOS/ParaEQ 301" 2>/dev/null || true
  open "$STANDALONE_SRC"
fi

if [[ "${DEPLOY_NO_PUSH:-}" == "1" ]]; then
  echo ""
  echo "deploy.sh: done (DEPLOY_NO_PUSH=1 — skipped git)."
  echo "  AU:   $AU_DST/$AU_NAME"
  echo "  VST3: $VST3_DST/$VST3_NAME"
  exit 0
fi

echo "deploy.sh: (4/4) git commit (if needed) & push to origin"
if ! git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "deploy.sh: error: not a git repository — cannot push." >&2
  exit 1
fi
if ! git -C "$ROOT" remote get-url origin >/dev/null 2>&1; then
  echo "deploy.sh: error: git remote 'origin' is not configured." >&2
  exit 1
fi

BRANCH="$(git -C "$ROOT" rev-parse --abbrev-ref HEAD)"
if [[ -n "$(git -C "$ROOT" status --porcelain)" ]]; then
  git -C "$ROOT" add -A
  git -C "$ROOT" commit -m "$COMMIT_MSG"
else
  echo "deploy.sh: working tree clean — no commit."
fi

git -C "$ROOT" push -u origin "$BRANCH"

echo ""
echo "deploy.sh: done."
echo "  AU:   $AU_DST/$AU_NAME"
echo "  VST3: $VST3_DST/$VST3_NAME"
echo "  git:  pushed branch $BRANCH to origin"
