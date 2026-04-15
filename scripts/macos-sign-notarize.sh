#!/usr/bin/env bash
#
# Sign (Developer ID) + notarize + staple the ParaEQ 301 AU, VST3, and installer PKG.
#
# Prerequisites (Apple Developer Program, ~$99/yr):
#   1) "Developer ID Application" certificate (Keychain) — signs .component / .vst3 Mach-O
#   2) "Developer ID Installer" certificate — signs the .pkg
#   3) App Store Connect API key for notarization (recommended) OR app-specific password
#
# Environment (API key method — good for CI):
#   SIGN_IDENTITY          e.g. "Developer ID Application: Your Name (12345678)"
#   INSTALLER_IDENTITY     e.g. "Developer ID Installer: Your Name (12345678)"
#   AU_PATH                Path to "ParaEQ 301.component"
#   VST_PATH               Path to "ParaEQ 301.vst3"
#   PKG_OUT                Output signed+stapled .pkg path (default: ParaEQ301-signed.pkg)
#   NOTARY_KEY_PATH        Path to AuthKey_XXXXXXXXXX.p8
#   NOTARY_KEY_ID          Key ID (10 chars)
#   NOTARY_ISSUER_ID       Issuer UUID from App Store Connect → Users → Keys
#
# Optional:
#   ENTITLEMENTS           Path to .entitlements for codesign (if your build needs it)
#   PKG_IDENTIFIER         defaults com.paraeq.paraeq301.plugins
#   PKG_VERSION            defaults 1.0.0
#
# One-time local: store notary credentials for notarytool
#   xcrun notarytool store-credentials "paraEQ-notary" --key "$NOTARY_KEY_PATH" \
#     --key-id "$NOTARY_KEY_ID" --issuer "$NOTARY_ISSUER_ID"
# Then set NOTARY_PROFILE="paraEQ-notary" instead of the three NOTARY_* vars.
#
# GitHub Actions: store NOTARY_KEY as secret (base64 p8), SIGNING_CERT base64 p12, etc.
# Or use a dedicated action: apple-actions/import-codesign-certs + run this script.
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SIGN_IDENTITY="${SIGN_IDENTITY:?Set SIGN_IDENTITY (Developer ID Application)}"
INSTALLER_IDENTITY="${INSTALLER_IDENTITY:?Set INSTALLER_IDENTITY (Developer ID Installer)}"
AU_PATH="${AU_PATH:-$ROOT/build/ParaEQ301_artefacts/Release/AU/ParaEQ 301.component}"
VST_PATH="${VST_PATH:-$ROOT/build/ParaEQ301_artefacts/Release/VST3/ParaEQ 301.vst3}"
PKG_OUT="${PKG_OUT:-$ROOT/ParaEQ301-signed-stapled.pkg}"
PKG_IDENTIFIER="${PKG_IDENTIFIER:-com.paraeq.paraeq301.plugins}"
PKG_VERSION="${PKG_VERSION:-1.0.0}"

if [[ ! -d "$AU_PATH" ]] || [[ ! -d "$VST_PATH" ]]; then
  echo "Missing AU or VST3. Build first: ./start.sh"
  echo "  AU_PATH=$AU_PATH"
  echo "  VST_PATH=$VST_PATH"
  exit 1
fi

sign_bundle() {
  local target="$1"
  local extra=()
  if [[ -n "${ENTITLEMENTS:-}" ]]; then
    extra=(--entitlements "$ENTITLEMENTS")
  fi
  codesign --force --deep --sign "$SIGN_IDENTITY" --timestamp \
    --options runtime "${extra[@]}" "$target"
  codesign --verify --verbose=4 "$target" 2>/dev/null || true
}

echo "==> Signing AU…"
sign_bundle "$AU_PATH"
echo "==> Signing VST3…"
sign_bundle "$VST_PATH"

PKG_ROOT="$ROOT/pkgroot-sign"
rm -rf "$PKG_ROOT"
mkdir -p "$PKG_ROOT/Library/Audio/Plug-Ins/Components"
mkdir -p "$PKG_ROOT/Library/Audio/Plug-Ins/VST3"
ditto "$AU_PATH" "$PKG_ROOT/Library/Audio/Plug-Ins/Components/$(basename "$AU_PATH")"
ditto "$VST_PATH" "$PKG_ROOT/Library/Audio/Plug-Ins/VST3/$(basename "$VST_PATH")"

PKG_UNSIGNED="$ROOT/ParaEQ301-unsigned-inner.pkg"
rm -f "$PKG_UNSIGNED" "$PKG_OUT"

echo "==> pkgbuild…"
pkgbuild --root "$PKG_ROOT" --identifier "$PKG_IDENTIFIER" --version "$PKG_VERSION" \
  --install-location / "$PKG_UNSIGNED"

echo "==> productsign (installer certificate)…"
productsign --sign "$INSTALLER_IDENTITY" --timestamp "$PKG_UNSIGNED" "$PKG_OUT"
rm -f "$PKG_UNSIGNED"

echo "==> notarytool submit (wait)…"
if [[ -n "${NOTARY_PROFILE:-}" ]]; then
  xcrun notarytool submit "$PKG_OUT" --keychain-profile "$NOTARY_PROFILE" --wait
else
  [[ -n "${NOTARY_KEY_PATH:-}" && -n "${NOTARY_KEY_ID:-}" && -n "${NOTARY_ISSUER_ID:-}" ]]
  xcrun notarytool submit "$PKG_OUT" \
    --key "$NOTARY_KEY_PATH" \
    --key-id "$NOTARY_KEY_ID" \
    --issuer "$NOTARY_ISSUER_ID" \
    --wait
fi

echo "==> stapler staple…"
xcrun stapler staple "$PKG_OUT"

echo "Done: $PKG_OUT"
spctl --assess --verbose --type install "$PKG_OUT" || true
