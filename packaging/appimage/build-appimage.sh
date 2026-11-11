#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-appimage}"
APPDIR="${APPDIR:-$ROOT_DIR/dist/Jotilla.AppDir}"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/dist}"

echo "==> Configuring build"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR"

echo "==> Staging AppDir at $APPDIR"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/scalable/apps"

install -Dm755 "$BUILD_DIR/jotilla" "$APPDIR/usr/bin/jotilla"
install -Dm644 "$ROOT_DIR/packaging/jotilla.desktop" \
    "$APPDIR/usr/share/applications/jotilla.desktop"
install -Dm644 "$ROOT_DIR/packaging/jotilla.svg" \
    "$APPDIR/usr/share/icons/hicolor/scalable/apps/jotilla.svg"

cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
exec "$HERE/usr/bin/jotilla" "$@"
EOF
chmod +x "$APPDIR/AppRun"

if command -v appimagetool >/dev/null 2>&1; then
    echo "==> Running appimagetool"
    mkdir -p "$OUT_DIR"
    appimagetool "$APPDIR" "$OUT_DIR/Jotilla-x86_64.AppImage"
    echo "==> AppImage created: $OUT_DIR/Jotilla-x86_64.AppImage"
else
    echo "appimagetool not found; the AppDir is ready at: $APPDIR"
    echo "Install appimagetool and run:"
    echo "  appimagetool \"$APPDIR\" \"$OUT_DIR/Jotilla-x86_64.AppImage\""
fi
