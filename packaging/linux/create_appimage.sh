#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUNDLE_DIR="$ROOT/build/portable/XDT-1.4.1-linux-x86_64"
APPDIR="$ROOT/build/appimage/AppDir"
TOOL_DIR="$ROOT/tools"
APPIMAGETOOL="$TOOL_DIR/appimagetool-x86_64.AppImage"
OUTPUT="$ROOT/build/XDT-1.4.1-linux-x86_64.AppImage"

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
