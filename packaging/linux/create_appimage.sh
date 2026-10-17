#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEFAULT_VER="1.0.0"
if [[ -f "$ROOT/AppVersion.h" ]]; then
    EXTRACTED_VER="$(grep -E '^#define[[:space:]]+APP_VERSION[[:space:]]+' "$ROOT/AppVersion.h" | sed -E 's/.*"([^"]+)".*/\1/')"
    if [[ -n "$EXTRACTED_VER" ]]; then
        DEFAULT_VER="$EXTRACTED_VER"
    fi
fi
VERSION="${1:-$DEFAULT_VER}"
BUNDLE_DIR="$ROOT/build/portable/XDT-${VERSION}-linux-x86_64"
APPDIR="$ROOT/build/appimage/AppDir"
TOOL_DIR="$ROOT/tools"
APPIMAGETOOL="$TOOL_DIR/appimagetool-x86_64.AppImage"
OUTPUT="$ROOT/build/XDT-${VERSION}-linux-x86_64.AppImage"

mkdir -p "$TOOL_DIR" "$ROOT/build/appimage"

if [ ! -x "$APPIMAGETOOL" ]; then
    curl -L --fail -o "$APPIMAGETOOL" \
        "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"
    chmod +x "$APPIMAGETOOL"
fi

rm -rf "$APPDIR"
mkdir -p "$APPDIR"
cp -a "$BUNDLE_DIR/." "$APPDIR/"
cp "$ROOT/packaging/linux/dualdisplay.desktop" "$APPDIR/XDT.desktop"
cp "$ROOT/packaging/linux/XDT.svg" "$APPDIR/XDT.svg"
chmod +x "$APPDIR/AppRun"

"$APPIMAGETOOL" "$APPDIR" "$OUTPUT"
ls -lh "$OUTPUT"
