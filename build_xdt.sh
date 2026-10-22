#!/usr/bin/env bash
# Build XDT, bundle ICU runtime libraries, create a Debian package, optionally install.
# Usage: ./build_xdt.sh [VERSION] [--install]
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
VERSION="1.0.0"
if [[ -f "$ROOT/AppVersion.h" ]]; then
    EXTRACTED_VERSION="$(grep -E '^#define[[:space:]]+APP_VERSION[[:space:]]+' "$ROOT/AppVersion.h" | sed -E 's/.*"([^\"]+)".*/\1/')"
    if [[ -n "$EXTRACTED_VERSION" ]]; then
        VERSION="$EXTRACTED_VERSION"
    fi
fi
DO_INSTALL=0
for arg in "$@"; do
    case "$arg" in
        --install) DO_INSTALL=1 ;;
        -h|--help) echo "Usage: $0 [VERSION] [--install]"; exit 0 ;;
        -*) echo "ERROR: Unknown option: $arg" >&2; exit 2 ;;
        *) VERSION="$arg" ;;
    esac
done

BUILD_DIR="$ROOT/build"
ARCH="$(dpkg --print-architecture)"
BUNDLE_DIR="$BUILD_DIR/portable/XDT-${VERSION}-linux-$(uname -m)"
DEB_FILE="$BUILD_DIR/xdt_${VERSION}_${ARCH}.deb"
PACKAGER="$ROOT/packaging/linux/create_deb.sh"

die() { echo "ERROR: $*" >&2; exit 1; }
step() { printf '\\n==> %s\\n' "$*"; }

[[ -f "$ROOT/CMakeLists.txt" ]] || die "CMakeLists.txt not found in $ROOT"
[[ -f "$PACKAGER" ]] || die "Debian packaging script not found: $PACKAGER"
command -v cmake >/dev/null 2>&1 || die "cmake is not installed"
command -v fakeroot >/dev/null 2>&1 || die "fakeroot is required; install with: sudo apt install fakeroot"
command -v ldd >/dev/null 2>&1 || die "ldd is not installed"

JOBS=2
if command -v nproc >/dev/null 2>&1; then JOBS="$(nproc)"; fi

step "Configure XDT ($VERSION, Release)"
cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DXDT_PACKAGE_VERSION="$VERSION" \
    -DPORTABLE_LINUX_BUNDLE=ON

step "Build executable and portable bundle"
cmake --build "$BUILD_DIR" --target portable_linux_bundle --parallel "$JOBS"

[[ -x "$BUNDLE_DIR/xdt" ]] || die "Portable executable missing: $BUNDLE_DIR/xdt"
[[ -x "$BUNDLE_DIR/AppRun" ]] || die "Portable launcher missing: $BUNDLE_DIR/AppRun"
[[ -d "$BUNDLE_DIR/lib" ]] || die "Portable runtime library directory missing: $BUNDLE_DIR/lib"

step "Bundle ICU runtime dependencies required by XDT"
ICU_COUNT=0
while IFS= read -r lib; do
    [[ -n "$lib" && -f "$lib" ]] || continue
    cp -L -- "$lib" "$BUNDLE_DIR/lib/$(basename "$lib")"
    printf 'Included: %s\\n' "$(basename "$lib")"
    ICU_COUNT=$((ICU_COUNT + 1))
done < <(ldd "$BUILD_DIR/xdt" | awk '$1 ~ /^libicu/ && $2 == "=>" && $3 ~ /^\\// {print $3}' | sort -u)

[[ "$ICU_COUNT" -gt 0 ]] || die "No resolved ICU dependencies found in ldd output for $BUILD_DIR/xdt"

step "Create Debian package"
fakeroot bash "$PACKAGER" "$VERSION"
[[ -s "$DEB_FILE" ]] || die "Package was not created: $DEB_FILE"

step "Verify package contains ICU libraries"
dpkg-deb --contents "$DEB_FILE" | grep -i 'libicu' || die "ICU libraries are not present in the Debian package"
dpkg-deb --info "$DEB_FILE" >/dev/null
ls -lh "$DEB_FILE"
printf '\\nPackage created successfully:\\n  %s\\n' "$DEB_FILE"

if [[ "$DO_INSTALL" -eq 1 ]]; then
    step "Install package"
    TMP_DEB="/tmp/$(basename "$DEB_FILE")"
    install -m 0644 "$DEB_FILE" "$TMP_DEB"
    sudo apt install --reinstall "$TMP_DEB"
    rm -f "$TMP_DEB"
    printf '\\nInstalled. Launch with: xdt\\n'
else
    printf '\\nTo install locally, run:\\n  sudo apt install --reinstall "%s"\\n' "$DEB_FILE"
fi