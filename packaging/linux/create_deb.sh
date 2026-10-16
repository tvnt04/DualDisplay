#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUNDLE_DIR="$ROOT/portable/DualDisplay-1.4.1-linux-x86_64"
PKGDIR="$ROOT/build/deb/dualdisplay-1.4.1-amd64"
OUT="$ROOT/build/dualdisplay_1.4.1_amd64.deb"

rm -rf "$PKGDIR"
mkdir -p "$PKGDIR/DEBIAN" "$PKGDIR/usr/lib/dualdisplay" "$PKGDIR/usr/bin" "$PKGDIR/usr/share/applications"

cp -a "$BUNDLE_DIR/." "$PKGDIR/usr/lib/dualdisplay/"

cat > "$PKGDIR/usr/bin/dualdisplay" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
APPDIR="/usr/lib/dualdisplay"
export LD_LIBRARY_PATH="$APPDIR/lib/kaya:$APPDIR/lib:$APPDIR/lib/qt5${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$APPDIR/lib/qt5/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$APPDIR/lib/qt5/plugins/platforms"
exec "$APPDIR/DualDisplay" "$@"
EOF
chmod 755 "$PKGDIR/usr/bin/dualdisplay"

cp "$ROOT/packaging/linux/dualdisplay.desktop" "$PKGDIR/usr/share/applications/dualdisplay.desktop"

cat > "$PKGDIR/DEBIAN/control" <<'EOF'
Package: dualdisplay
Version: 1.4.1
Section: utils
Priority: optional
Architecture: amd64
Depends: libc6 (>= 2.31), libx11-6, libqt5core5a, libqt5gui5, libqt5widgets5
Maintainer: Your Name <you@example.com>
Description: DualDisplay camera analysis application
EOF
chmod 755 "$PKGDIR/DEBIAN/control"

dpkg-deb --build "$PKGDIR" "$OUT"
ls -lh "$OUT"
