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
ARCH="$(dpkg --print-architecture)"
MAINTAINER="${DEB_MAINTAINER:-TVNT04 <tvnt04@users.noreply.github.com>}"

BUNDLE_DIR="$ROOT/build/portable/XDT-${VERSION}-linux-x86_64"
PKGDIR="$ROOT/build/deb/xdt-${VERSION}-${ARCH}"
OUT="$ROOT/build/xdt_${VERSION}_${ARCH}.deb"

# Verify required build inputs.
if [[ ! -d "$BUNDLE_DIR" ]]; then
    echo "ERROR: Portable bundle not found: $BUNDLE_DIR" >&2
    exit 1
fi

if [[ ! -x "$BUNDLE_DIR/xdt" ]]; then
    echo "ERROR: XDT executable is missing or not executable." >&2
    exit 1
fi

if [[ ! -f "$ROOT/packaging/linux/icons/dualdisplay.png" ]]; then
    echo "ERROR: Debian application icon not found." >&2
    exit 1
fi

command -v dpkg-deb >/dev/null || {
    echo "ERROR: dpkg-deb is required." >&2
    exit 1
}

rm -rf "$PKGDIR"

mkdir -p \
    "$PKGDIR/DEBIAN" \
    "$PKGDIR/usr/lib/xdt" \
    "$PKGDIR/usr/bin" \
    "$PKGDIR/usr/share/applications" \
    "$PKGDIR/usr/share/icons/hicolor/512x512/apps"

# Install the portable application bundle.
cp -a "$BUNDLE_DIR/." "$PKGDIR/usr/lib/xdt/"

# Provide a stable command for launching the application.
cat > "$PKGDIR/usr/bin/xdt" <<'LAUNCHER'
#!/usr/bin/env bash
set -euo pipefail

APPDIR="/usr/lib/xdt"

export LD_LIBRARY_PATH="$APPDIR/lib/kaya:$APPDIR/lib:$APPDIR/lib/qt5${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$APPDIR/lib/qt5/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$APPDIR/lib/qt5/plugins/platforms"

exec "$APPDIR/xdt" "$@"
LAUNCHER

chmod 755 "$PKGDIR/usr/bin/xdt"

# Use a Debian-specific desktop entry without changing AppImage behavior.
sed \
    -e 's/^Exec=AppRun$/Exec=xdt/' \
    -e 's/^Icon=DualDisplay$/Icon=dualdisplay/' \
    "$ROOT/packaging/linux/dualdisplay.desktop" \
    > "$PKGDIR/usr/share/applications/xdt.desktop"

# Install the black-background application logo.
install -m 644 \
    "$ROOT/packaging/linux/icons/dualdisplay.png" \
    "$PKGDIR/usr/share/icons/hicolor/512x512/apps/xdt.png"

# Package metadata.
cat > "$PKGDIR/DEBIAN/control" <<CONTROL
Package: xdt
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: ${ARCH}
Depends: libc6 (>= 2.31), libx11-6, libqt5core5a, libqt5gui5, libqt5widgets5
Maintainer: ${MAINTAINER}
Description: XDLINX Display Tool camera analysis application
 XDLINX Display Tool is a desktop application for camera analysis.
CONTROL

chmod 644 "$PKGDIR/DEBIAN/control"

# Build the Debian package.
mkdir -p "$(dirname "$OUT")"
# Build package contents with standard root ownership metadata.
if [[ $EUID -eq 0 ]]; then
    chown -R root:root "$PKGDIR"
else
    fakeroot chown -R root:root "$PKGDIR"
fi
find "$PKGDIR" -type d -exec chmod 755 {} +
find "$PKGDIR" -type f -exec chmod 644 {} +
chmod 755 "$PKGDIR/usr/bin/xdt" "$PKGDIR/usr/lib/xdt/AppRun" "$PKGDIR/usr/lib/xdt/xdt"
dpkg-deb --build "$PKGDIR" "$OUT"

echo
echo "Debian package created:"
ls -lh "$OUT"
echo
echo "Package metadata:"
dpkg-deb --info "$OUT"
