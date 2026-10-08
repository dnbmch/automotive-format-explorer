#!/usr/bin/env bash
#
# Package the Linux build as an AppImage: the executable and the samples, with Qt
# and every library the executable needs deployed by linuxdeploy and its Qt
# plugin. CI, the release job and the srv-one check all run it.
#
# The executable must link libGL.so.1, as Qt's own libraries do. linuxdeploy
# leaves libOpenGL.so.0 to the system, and a desktop without the libopengl0
# package lacks it, so an executable naming it fails the package.
#
# Usage:
#   bash scripts/package_linux.sh [build-dir] [out-dir]
#
# Env: QT_PREFIX (the Qt gcc_64 root), VERSION (the file name's version, default dev)

set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="$(cd "${1:-build}" && pwd)"
mkdir -p "${2:-.}"
OUT="$(cd "${2:-.}" && pwd)"
: "${QT_PREFIX:?Set QT_PREFIX to the Qt installation}"
VERSION="${VERSION:-dev}"
APP=automotive-format-explorer

# linuxdeploy and its Qt plugin, pinned to tagged builds. They run extracted, so
# the packaging host needs no FUSE.
LINUXDEPLOY=https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage
LINUXDEPLOY_QT=https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage
export APPIMAGE_EXTRACT_AND_RUN=1

if readelf -d "$BUILD_DIR/$APP" | grep -q 'NEEDED.*\[libOpenGL\.so\.0\]'; then
    echo "$APP names libOpenGL.so.0, which a desktop without libopengl0 lacks" >&2
    exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# --- Application payload -------------------------------------------------
#
# The samples resolve from usr/bin/../share/<app>/samples at runtime.

mkdir -p "$WORK/AppDir/usr/share/$APP"
cp -r samples "$WORK/AppDir/usr/share/$APP/samples"
cp resources/icons/explorer_256.png "$WORK/$APP.png"
cat > "$WORK/$APP.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Automotive Format Explorer
Comment=Inspect A2L, DBC, LDF and MDF4 automotive files
Exec=automotive-format-explorer %F
Icon=automotive-format-explorer
Categories=Development;Engineering;
DESKTOP

# --- Qt and the library closure ------------------------------------------

curl -fsSL -o "$WORK/linuxdeploy-x86_64.AppImage" "$LINUXDEPLOY"
curl -fsSL -o "$WORK/linuxdeploy-plugin-qt-x86_64.AppImage" "$LINUXDEPLOY_QT"
chmod +x "$WORK"/linuxdeploy-*.AppImage

QML="$PWD/qml"
(
    cd "$WORK"
    QMAKE="$QT_PREFIX/bin/qmake" \
    QML_SOURCES_PATHS="$QML" \
    LD_LIBRARY_PATH="$QT_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        ./linuxdeploy-x86_64.AppImage \
            --appdir AppDir \
            --executable "$BUILD_DIR/$APP" \
            --desktop-file "$APP.desktop" \
            --icon-file "$APP.png" \
            --plugin qt \
            --output appimage
)

# linuxdeploy writes the AppImage into its working directory, named after the
# desktop entry.
mv "$WORK"/Automotive_Format_Explorer-*.AppImage "$OUT/$APP-$VERSION-linux-x86_64.AppImage"
echo "packaged $OUT/$APP-$VERSION-linux-x86_64.AppImage"
