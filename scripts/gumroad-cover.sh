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
THUMB="$COVERDIR/paraeq-og.png"   # square social card → Gumroad thumbnail → og:image

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
# Square social card (plugin face + title) — Gumroad requires a SQUARE thumbnail, and the
# thumbnail is what becomes og:image, so the card must be square (not 1.91:1).
FONT="/System/Library/Fonts/Helvetica.ttc"
FONTB="/System/Library/Fonts/HelveticaNeue.ttc"; [ -f "$FONTB" ] || FONTB="$FONT"
echo "gumroad-cover.sh: composing square og card → $THUMB"
magick -size 1200x1200 xc:'#0d0f12' \
  \( "$COVER" -resize 1100x \) -gravity North -geometry +0+34 -composite \
  -gravity North \
  -font "$FONTB" -fill '#f3f1ea' -pointsize 70 -annotate +0+1010 'ParaEQ 301' \
  -font "$FONT"  -fill '#d7d3c8' -pointsize 33 -annotate +0+1100 'Finishes the sound — and makes it move.' \
  -font "$FONTB" -fill '#ff7a1a' -pointsize 30 -annotate +0+1150 'AU · VST3 · macOS' \
  "$THUMB"

echo "gumroad-cover.sh: clearing existing covers…"
gumroad products view "$PRODUCT_ID" --json --no-input --jq '.product.covers[].id' 2>/dev/null | tr -d '"' | while IFS= read -r c; do
  [ -n "$c" ] && gumroad products covers remove --yes --json --no-input --quiet -- "$PRODUCT_ID" "$c" >/dev/null 2>&1 || true
done

echo "gumroad-cover.sh: setting cover + thumbnail"
gumroad products covers add "$PRODUCT_ID" --image "$COVER" --json --no-input --quiet >/dev/null
gumroad products thumbnail set "$PRODUCT_ID" --image "$THUMB" --json --no-input --quiet >/dev/null

N="$(gumroad products view "$PRODUCT_ID" --json --no-input --jq '.product.covers | length' 2>/dev/null || echo '?')"

# MAINTAIN LIVE: cover/thumbnail ops must never leave the product as a draft. If a file
# is attached, ensure it stays published. (Esa: "this must maintain live.")
FILES="$(gumroad products view "$PRODUCT_ID" --json --no-input --jq '.product.files | length' 2>/dev/null || echo 0)"
PUB="$(gumroad products view "$PRODUCT_ID" --json --no-input --jq '.product.published' 2>/dev/null || echo '?')"
if [ "$PUB" != "true" ] && [ "${FILES:-0}" -ge 1 ] 2>/dev/null; then
  echo "gumroad-cover.sh: product went draft — re-publishing to stay live"
  gumroad products publish --json --no-input --quiet -- "$PRODUCT_ID" >/dev/null 2>&1
  PUB="$(gumroad products view "$PRODUCT_ID" --json --no-input --jq '.product.published' 2>/dev/null || echo '?')"
fi

echo "gumroad-cover.sh: done — $N cover(s), square og card set, published=$PUB."
[ "$N" = "1" ] || { echo "gumroad-cover.sh: WARNING expected 1 cover, got $N" >&2; exit 1; }
