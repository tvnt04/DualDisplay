#!/usr/bin/env bash
# Build XDT, stage its portable Linux bundle, and create a Debian package.
# Usage:
#   ./build_xdt.sh              # build the .deb
#   ./build_xdt.sh --install    # build the .deb and install it
#   ./build_xdt.sh 1.4.2        # build a package with the supplied package version
#   ./build_xdt.sh 1.4.2 --install
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_VER="1.0.0"
if [[ -f "$ROOT/AppVersion.h" ]]; then
    EXTRACTED_VER="$(grep -E '^#define[[:space:]]+APP_VERSION[[:space:]]+' "$ROOT/AppVersion.h" | sed -E 's/.*"([^"]+)".*/\1/')"
    if [[ -n "$EXTRACTED_VER" ]]; then
        DEFAULT_VER="$EXTRACTED_VER"
    fi
fi

VERSION=""
DO_INSTALL=0

for arg in "$@"; do
    case "$arg" in
        --install) DO_INSTALL=1 ;;
        -h|--help)
            sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        -*)
            echo "ERROR: Unknown option: $arg" >&2
            exit 2
            ;;
        *)
            VERSION="$arg"
            ;;
    esac
done

if [[ -z "$VERSION" ]]; then
    VERSION="$DEFAULT_VER"
fi

BUILD_DIR="$ROOT/build"
ARCH="$(dpkg --print-architecture)"
BUNDLE_DIR="$BUILD_DIR/portable/XDT-${VERSION}-linux-$(uname -m)"
DEB_FILE="$BUILD_DIR/xdt_${VERSION}_${ARCH}.deb"
PACKAGER="$ROOT/packaging/linux/create_deb.sh"

die() { echo "ERROR: $*" >&2; exit 1; }
step() { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }

[[ -f "$ROOT/CMakeLists.txt" ]] || die "CMakeLists.txt not found in $ROOT"
[[ -f "$PACKAGER" ]] || die "Debian packaging script not found: $PACKAGER"
command -v cmake >/dev/null 2>&1 || die "cmake is not installed"
command -v dpkg >/dev/null 2>&1 || die "dpkg is not installed"
command -v fakeroot >/dev/null 2>&1 || die "fakeroot is required; install it with: sudo apt install fakeroot"
command -v nproc >/dev/null 2>&1 || true

JOBS=2
if command -v nproc >/dev/null 2>&1; then JOBS="$(nproc)"; fi

step "Configure XDT ($VERSION, Release, Real Hardware)"
cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DUSING_STUB=OFF \
    -DPORTABLE_LINUX_BUNDLE=ON

step "Compile XDT and build the portable runtime bundle"
cmake --build "$BUILD_DIR" --target portable_linux_bundle --parallel "$JOBS"
cp -f "$BUILD_DIR/xdt" "$ROOT/xdt"

[[ -x "$BUNDLE_DIR/xdt" ]] || die "Portable executable missing: $BUNDLE_DIR/xdt"
[[ -x "$BUNDLE_DIR/AppRun" ]] || die "Portable launcher missing: $BUNDLE_DIR/AppRun"
[[ -d "$BUNDLE_DIR/lib" ]] || die "Portable runtime libraries missing: $BUNDLE_DIR/lib"

step "Create Debian package"
fakeroot bash "$PACKAGER" "$VERSION"

[[ -s "$DEB_FILE" ]] || die "Package was not created: $DEB_FILE"

step "Verify package metadata"
dpkg-deb --info "$DEB_FILE"
printf '\nPackage created successfully:\n  %s\n' "$DEB_FILE"
ls -lh "$DEB_FILE"

if [[ "$DO_INSTALL" -eq 1 ]]; then
    step "Install package"
    TMP_DEB="/tmp/$(basename "$DEB_FILE")"
    install -m 0644 "$DEB_FILE" "$TMP_DEB"
    sudo apt install "$TMP_DEB"
    rm -f "$TMP_DEB"
    printf '\nXDT package installed. Launch it with: xdt\n'
else
    printf '\nTo install this package, run:\n  sudo apt install "%s"\n' "$DEB_FILE"
fi

