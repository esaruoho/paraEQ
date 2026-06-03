#!/usr/bin/env bash
# snap-ui.sh — launch the ParaEQ 301 Standalone and capture its window to a PNG.
# Used as a Convey mechanism (build evidence + Gumroad cover candidate).
#
# Produces: dist/shots/paraeq-ui-YYYYMMDD-HHMMSS.png  (+ dist/LATEST_SHOT.txt)
#
# Apple-native: uses ~/work/apple/bin/window-frame (CGWindowList geometry) +
# /usr/sbin/screencapture -R. No Quartz/pip. Needs Screen Recording permission
# for the calling process (Terminal/Claude) — same as every /snap capture.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/build/ParaEQ301_artefacts/Release/Standalone/ParaEQ 301.app"
WF="$HOME/work/apple/bin/window-frame"
DIST="$ROOT/dist"
TS="$(date +%Y%m%d-%H%M%S)"
OUT="$DIST/shots/paraeq-ui-$TS.png"

[[ -d "$APP" ]] || { echo "snap-ui.sh: missing app: $APP (run ./start.sh)" >&2; exit 1; }
mkdir -p "$DIST/shots"

echo "snap-ui.sh: launching Standalone…"
xattr -dr com.apple.quarantine "$APP" 2>/dev/null || true
open "$APP"

# Wait for the window to appear in CGWindowList (up to ~10s).
geom=""
for _ in $(seq 1 20); do
  sleep 0.5
  if [[ -x "$WF" ]]; then
    geom="$($WF --json "ParaEQ" 2>/dev/null | /usr/bin/python3 -c '
import sys, json
try:
    data = json.load(sys.stdin)
except Exception:
    sys.exit(0)
wins = data.get("windows", []) if isinstance(data, dict) else (data if isinstance(data, list) else [])
best = None
for w in wins:
    if "paraeq" in str(w.get("owner","")).lower():
        a = float(w.get("w",0)) * float(w.get("h",0))
        if best is None or a > best[0]:
            best = (a, w)
if best:
    w = best[1]
    print("%d,%d,%d,%d" % (int(w["x"]), int(w["y"]), int(w["w"]), int(w["h"])))
' 2>/dev/null || true)"
  fi
  [[ -n "$geom" ]] && break
done

if [[ -n "$geom" ]]; then
  echo "snap-ui.sh: capturing window region $geom"
  /usr/sbin/screencapture -x -R"$geom" "$OUT"
else
  echo "snap-ui.sh: window geometry not found — capturing full screen as fallback"
  /usr/sbin/screencapture -x "$OUT"
fi

printf '%s\n' "$OUT" > "$DIST/LATEST_SHOT.txt"
echo "snap-ui.sh: done → $OUT"
