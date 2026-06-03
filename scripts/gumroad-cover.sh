#!/usr/bin/env bash
# gumroad-cover.sh — make a clean cover + square thumbnail from the UI screenshot and
# set them on the Gumroad product, ending with EXACTLY ONE cover. Requires ImageMagick.
#
# Clean = the title bar + the yellow "audio input muted" standalone banner are cropped
# off, leaving just the plugin face. Uses dist/LATEST_SHOT.txt (from snap-ui.sh); if
# absent, captures one first.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PRODUCT_ID="${PARAEQ_GUMROAD_ID:--QuWS2I6mnM78PysotOKqw==}"
COVERDIR="$ROOT/dist/cover"
COVER="$COVERDIR/paraeq-cover.png"
THUMB="$COVERDIR/paraeq-thumb.png"

command -v magick >/dev/null 2>&1 || { echo "gumroad-cover.sh: ImageMagick (magick) not found" >&2; exit 1; }

SHOT="$(cat "$ROOT/dist/LATEST_SHOT.txt" 2>/dev/null || true)"
if [ -z "$SHOT" ] || [ ! -f "$SHOT" ]; then
  echo "gumroad-cover.sh: no shot — capturing…"
  "$ROOT/scripts/snap-ui.sh" >/dev/null
  SHOT="$(cat "$ROOT/dist/LATEST_SHOT.txt")"
fi

mkdir -p "$COVERDIR"
H="$(sips -g pixelHeight "$SHOT" 2>/dev/null | awk '/pixelHeight/{print $2}')"
W="$(sips -g pixelWidth  "$SHOT" 2>/dev/null | awk '/pixelWidth/{print $2}')"
# Title bar + standalone banner ≈ 104px of a 1696px-tall retina capture → crop proportionally.
CROPTOP=$(( H * 104 / 1696 ))
NEWH=$(( H - CROPTOP ))
echo "gumroad-cover.sh: cropping chrome (${W}x${H} → ${W}x${NEWH}, top ${CROPTOP}px)"
magick "$SHOT" -crop "${W}x${NEWH}+0+${CROPTOP}" +repage "$COVER"
# Square thumbnail: whole face fit into 1024² on a UI-dark background.
magick "$COVER" -resize 1024x1024 -background "#0b0f14" -gravity center -extent 1024x1024 "$THUMB"

echo "gumroad-cover.sh: clearing existing covers…"
gumroad products view "$PRODUCT_ID" --json --no-input --jq '.product.covers[].id' 2>/dev/null | tr -d '"' | while IFS= read -r c; do
  [ -n "$c" ] && gumroad products covers remove --yes --json --no-input --quiet -- "$PRODUCT_ID" "$c" >/dev/null 2>&1 || true
done

echo "gumroad-cover.sh: setting cover + thumbnail"
gumroad products covers add "$PRODUCT_ID" --image "$COVER" --json --no-input --quiet >/dev/null
gumroad products thumbnail set "$PRODUCT_ID" --image "$THUMB" --json --no-input --quiet >/dev/null

N="$(gumroad products view "$PRODUCT_ID" --json --no-input --jq '.product.covers | length' 2>/dev/null || echo '?')"
echo "gumroad-cover.sh: done — $N cover(s), thumbnail set."
[ "$N" = "1" ] || { echo "gumroad-cover.sh: WARNING expected 1 cover, got $N" >&2; exit 1; }
