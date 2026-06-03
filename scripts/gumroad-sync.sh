#!/usr/bin/env bash
# gumroad-sync.sh — push everything the Gumroad API *can* set for the ParaEQ
# product: HTML description (from features/paraeq.listing.html), tags, price.
# The product FILE itself cannot be set via the API — that is the staged manual
# drop (see scripts/gumroad-stage.sh). This script is idempotent.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PRODUCT_ID="${PARAEQ_GUMROAD_ID:--QuWS2I6mnM78PysotOKqw==}"
DESC_FILE="$ROOT/features/paraeq.listing.html"

[[ -f "$DESC_FILE" ]] || { echo "gumroad-sync.sh: missing $DESC_FILE" >&2; exit 1; }

echo "gumroad-sync.sh: syncing metadata to product $PRODUCT_ID"
gumroad products update "$PRODUCT_ID" \
  --description "$(cat "$DESC_FILE")" \
  --price 135.40 --currency eur \
  --tag "eq" --tag "audio plugin" --tag "vst3" --tag "au" --tag "mixing" --tag "saturation" \
  --json --no-input --quiet >/dev/null

gumroad products view "$PRODUCT_ID" --json --no-input \
  --jq '.product | "  name: \(.name)\n  price: \(.formatted_price)\n  published: \(.published)\n  tags: \(.tags|join(", "))"' --no-color
echo "gumroad-sync.sh: done."
