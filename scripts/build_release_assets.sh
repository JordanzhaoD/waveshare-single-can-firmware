#!/usr/bin/env bash
# Build the standard 8-asset release bundle for the Waveshare single-CAN firmware.
# Runs identically in CI (release.yml) and locally — the single source of truth
# for the release format.
#
# Usage:
#   scripts/build_release_assets.sh [BUILD_DIR] [OUT_DIR] [VERSION]
#     BUILD_DIR  default: .pio/build/waveshare_single_can_standalone
#     OUT_DIR    default: release-assets
#     VERSION    default: contents of ./VERSION
#
# Produces in OUT_DIR:
#   bootloader.bin                      (PIO build artifact)
#   partitions.bin                      (PIO build artifact)
#   ota_data_initial.bin                (PIO build artifact)
#   firmware.bin                        (PIO build artifact)
#   firmware-waveshare-single-can.bin   (= firmware.bin, OTA release name)
#   merged-flash.bin                    (esptool merge_bin of the 4 above)
#   flash.sh                            (from scripts/flash.sh.template, version-stamped)
#   SHA256SUMS                          (sha256 of all of the above)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-$ROOT/.pio/build/waveshare_single_can_standalone}"
OUT_DIR="${2:-$ROOT/release-assets}"
VERSION="${3:-$(tr -d '[:space:]' < "$ROOT/VERSION")}"
TAG="v${VERSION}-atlas-single-can"

need() { [ -f "$BUILD_DIR/$1" ] || { echo "❌ missing $BUILD_DIR/$1 — run 'pio run -e waveshare_single_can_standalone' first" >&2; exit 1; }; }
need bootloader.bin
need partitions.bin
need ota_data_initial.bin
need firmware.bin

echo "📦 Building release assets for $TAG from $BUILD_DIR → $OUT_DIR"
mkdir -p "$OUT_DIR"
rm -f "$OUT_DIR"/*.bin "$OUT_DIR"/flash.sh "$OUT_DIR"/SHA256SUMS

cp "$BUILD_DIR/bootloader.bin"         "$OUT_DIR/"
cp "$BUILD_DIR/partitions.bin"         "$OUT_DIR/"
cp "$BUILD_DIR/ota_data_initial.bin"   "$OUT_DIR/"
cp "$BUILD_DIR/firmware.bin"           "$OUT_DIR/"
cp "$BUILD_DIR/firmware.bin"           "$OUT_DIR/firmware-waveshare-single-can.bin"

# merged-flash.bin — one-shot flash image (recommended for end users)
python3 -m esptool --chip esp32s3 merge_bin -o "$OUT_DIR/merged-flash.bin" \
  --flash_mode dio --flash_size keep --flash_freq 40m \
  0x0      "$OUT_DIR/bootloader.bin" \
  0x8000   "$OUT_DIR/partitions.bin" \
  0x19000  "$OUT_DIR/ota_data_initial.bin" \
  0x20000  "$OUT_DIR/firmware.bin" >/dev/null

# flash.sh — version-stamped from the template
sed "s/__VERSION__/${VERSION}/g" "$ROOT/scripts/flash.sh.template" > "$OUT_DIR/flash.sh"
chmod +x "$OUT_DIR/flash.sh"

# SHA256SUMS — portable across linux (sha256sum) and mac (shasum -a 256)
SHA_CMD="sha256sum"
command -v sha256sum >/dev/null 2>&1 || SHA_CMD="shasum -a 256"
( cd "$OUT_DIR" && $SHA_CMD \
    bootloader.bin partitions.bin ota_data_initial.bin \
    firmware.bin firmware-waveshare-single-can.bin \
    merged-flash.bin flash.sh > SHA256SUMS )

echo "✅ Built 8 assets for $TAG:"
ls -la "$OUT_DIR/"
echo ""
echo "SHA256SUMS:"
cat "$OUT_DIR/SHA256SUMS"
