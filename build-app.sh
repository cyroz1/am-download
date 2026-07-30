#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
APP_DIR="$BUILD_DIR/AM Download.app"
CONTENTS_DIR="$APP_DIR/Contents"
MODULE_CACHE_DIR="$BUILD_DIR/ModuleCache"
ICONSET_DIR="$BUILD_DIR/AppIcon.iconset"
ICON_BASE="$BUILD_DIR/AppIcon-1024.png"
ICON_GENERATOR="$BUILD_DIR/generate-icon"
ICNS_GENERATOR="$BUILD_DIR/generate-icns"

mkdir -p "$CONTENTS_DIR/MacOS" "$CONTENTS_DIR/Resources" "$MODULE_CACHE_DIR" "$ICONSET_DIR"

xcrun swiftc \
    -parse-as-library \
    -O \
    -module-cache-path "$MODULE_CACHE_DIR" \
    -target arm64-apple-macos13.0 \
    -framework SwiftUI \
    -framework AppKit \
    "$ROOT_DIR/Sources/AMDownload/AMDownloadApp.swift" \
    -o "$CONTENTS_DIR/MacOS/AMDownload"

cp "$ROOT_DIR/Resources/Info.plist" "$CONTENTS_DIR/Info.plist"

xcrun swiftc \
    -parse-as-library \
    -O \
    -module-cache-path "$MODULE_CACHE_DIR" \
    -framework AppKit \
    "$ROOT_DIR/Tools/GenerateIcon.swift" \
    -o "$ICON_GENERATOR"
"$ICON_GENERATOR" "$ICON_BASE"

for size in 16 32 128 256 512; do
    sips -s format png -z "$size" "$size" "$ICON_BASE" \
        --out "$ICONSET_DIR/icon_${size}x${size}.png" >/dev/null
    double_size=$((size * 2))
    sips -s format png -z "$double_size" "$double_size" "$ICON_BASE" \
        --out "$ICONSET_DIR/icon_${size}x${size}@2x.png" >/dev/null
done

xcrun swiftc \
    -parse-as-library \
    -O \
    -module-cache-path "$MODULE_CACHE_DIR" \
    "$ROOT_DIR/Tools/GenerateICNS.swift" \
    -o "$ICNS_GENERATOR"
"$ICNS_GENERATOR" "$ICONSET_DIR" "$CONTENTS_DIR/Resources/AppIcon.icns"

codesign --force --deep --sign - "$APP_DIR"

echo "Built: $APP_DIR"
