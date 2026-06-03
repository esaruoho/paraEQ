#!/usr/bin/env bash
# gumroad-upload.sh — attach the freshly-built zip to the Gumroad product's Download
# area, DETERMINISTICALLY ending with exactly ONE file. Requires gumroad CLI >= 0.12.
#
# The CLI's `--file` + `--file-name` can leave a phantom second entry, and
# `--replace-files` does not always collapse to one, so this script is explicit:
#   1. remove every existing file   2. attach the one zip   3. prune any extras.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PRODUCT_ID="${PARAEQ_GUMROAD_ID:--QuWS2I6mnM78PysotOKqw==}"
ZIP="$(cat "$ROOT/dist/LATEST_ZIP.txt" 2>/dev/null || true)"

[[ -n "$ZIP" && -f "$ZIP" ]] || { echo "gumroad-upload.sh: no built zip — run ./package.sh first" >&2; exit 1; }
ZSIZE="$(stat -f%z "$ZIP")"

# bash 3.2 (macOS) safe — no mapfile.
ids()       { gumroad products view "$PRODUCT_ID" --json --no-input --jq '.product.files[].id' 2>/dev/null | tr -d '"'; }
id_sizes()  { gumroad products view "$PRODUCT_ID" --json --no-input --jq '.product.files[] | "\(.id)\t\(.size)"' 2>/dev/null | tr -d '"'; }

echo "gumroad-upload.sh: clearing existing files…"
ids | while IFS= read -r id; do
  [ -n "$id" ] && gumroad products update --remove-file "$id" --yes --json --no-input --quiet -- "$PRODUCT_ID" >/dev/null 2>&1 || true
done

# Attach with --file ONLY. (--file-name induces a phantom size:null twin; basename is fine.)
# size/name populate ASYNChronously — do not race them; just confirm the count.
echo "gumroad-upload.sh: attaching $ZIP ($ZSIZE bytes)"
gumroad products update --file "$ZIP" --yes --json --no-input --quiet -- "$PRODUCT_ID" >/dev/null

# Safety net: if a stray twin ever appears, keep the largest-size entry, drop the rest.
COUNT="$(gumroad products view "$PRODUCT_ID" --json --no-input --jq '.product.files | length' 2>/dev/null || echo '?')"
if [ "${COUNT:-0}" -gt 1 ] 2>/dev/null; then
  KEEP="$(id_sizes | sort -t'	' -k2 -nr | awk -F'\t' 'NR==1{print $1}')"
  id_sizes | awk -F'\t' '{print $1}' | while IFS= read -r id; do
    [ -n "$id" ] && [ "$id" != "$KEEP" ] && gumroad products update --remove-file "$id" --yes --json --no-input --quiet -- "$PRODUCT_ID" >/dev/null 2>&1 || true
  done
  COUNT="$(gumroad products view "$PRODUCT_ID" --json --no-input --jq '.product.files | length' 2>/dev/null || echo '?')"
fi

echo "gumroad-upload.sh: files now (size/name settle a few seconds after upload):"
gumroad products view "$PRODUCT_ID" --json --no-input \
  --jq '.product.files[] | "  \(.name).\(.filetype)  \(.size) bytes  \(.url[0:60])…"' 2>&1
echo "gumroad-upload.sh: done — $COUNT file(s) attached."
[ "$COUNT" = "1" ] || { echo "gumroad-upload.sh: WARNING expected 1 file, got $COUNT" >&2; exit 1; }

# ── End the run FOR SALE ──────────────────────────────────────────────────────
# `products update` (above) drops a live product back to draft, and the pipeline
# would otherwise end "not currently for sale". Publish LAST. Only reached after
# the count check confirms exactly one file is attached, so we never publish a
# fileless (pay-and-get-nothing) product. (Esa's choice 2026-06-03: runs end live.)
echo "gumroad-upload.sh: publishing — ending for-sale…"
gumroad products publish --json --no-input --quiet -- "$PRODUCT_ID" >/dev/null
PUB="$(gumroad products view "$PRODUCT_ID" --json --no-input --jq '.product.published' 2>/dev/null || echo '?')"
echo "gumroad-upload.sh: published = $PUB"
[ "$PUB" = "true" ] || { echo "gumroad-upload.sh: WARNING expected published=true, got $PUB" >&2; exit 1; }
