#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

VERSION="${1:-1.4.1}"
ARCH="$(dpkg --print-architecture)"
MAINTAINER="${DEB_MAINTAINER:-TVNT04 <tvnt04@users.noreply.github.com>}"

BUNDLE_DIR="$ROOT/portable/DualDisplay-${VERSION}-linux-x86_64"
PKGDIR="$ROOT/build/deb/dualdisplay-${VERSION}-${ARCH}"
OUT="$ROOT/build/dualdisplay_${VERSION}_${ARCH}.deb"

# Verify required build inputs.
if [[ ! -d "$BUNDLE_DIR" ]]; then
    echo "ERROR: Portable bundle not found: $BUNDLE_DIR" >&2
    exit 1
fi

if [[ ! -x "$BUNDLE_DIR/DualDisplay" ]]; then
    echo "ERROR: DualDisplay executable is missing or not executable." >&2
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
    "$PKGDIR/usr/lib/dualdisplay" \
    "$PKGDIR/usr/bin" \
    "$PKGDIR/usr/share/applications" \
    "$PKGDIR/usr/share/icons/hicolor/512x512/apps"

# Install the portable application bundle.
cp -a "$BUNDLE_DIR/." "$PKGDIR/usr/lib/dualdisplay/"

# Provide a stable command for launching the application.
cat > "$PKGDIR/usr/bin/dualdisplay" <<'LAUNCHER'
#!/usr/bin/env bash
set -euo pipefail

APPDIR="/usr/lib/dualdisplay"

export LD_LIBRARY_PATH="$APPDIR/lib/kaya:$APPDIR/lib:$APPDIR/lib/qt5${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$APPDIR/lib/qt5/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$APPDIR/lib/qt5/plugins/platforms"

exec "$APPDIR/DualDisplay" "$@"
LAUNCHER

chmod 755 "$PKGDIR/usr/bin/dualdisplay"

# Use a Debian-specific desktop entry without changing AppImage behavior.
sed \
    -e 's/^Exec=AppRun$/Exec=dualdisplay/' \
    -e 's/^Icon=DualDisplay$/Icon=dualdisplay/' \
    "$ROOT/packaging/linux/dualdisplay.desktop" \
    > "$PKGDIR/usr/share/applications/dualdisplay.desktop"

# Install the black-background application logo.
install -m 644 \
    "$ROOT/packaging/linux/icons/dualdisplay.png" \
    "$PKGDIR/usr/share/icons/hicolor/512x512/apps/dualdisplay.png"

# Package metadata.
cat > "$PKGDIR/DEBIAN/control" <<CONTROL
Package: dualdisplay
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: ${ARCH}
Depends: libc6 (>= 2.31), libx11-6, libqt5core5a, libqt5gui5, libqt5widgets5
Maintainer: ${MAINTAINER}
Description: DualDisplay camera analysis application
 DualDisplay is a desktop application for camera analysis.
CONTROL

chmod 644 "$PKGDIR/DEBIAN/control"

# Build the Debian package.
mkdir -p "$(dirname "$OUT")"
dpkg-deb --build "$PKGDIR" "$OUT"

echo
echo "Debian package created:"
ls -lh "$OUT"
echo
echo "Package metadata:"
dpkg-deb --info "$OUT"
