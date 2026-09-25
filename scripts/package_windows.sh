#!/usr/bin/env bash
#
# Package the Windows build into a self-contained dist/ directory.
#
# Qt is deployed with windeployqt; everything else is resolved by
# scripts/deploy_closure.sh, which walks the dependency closure of the binaries
# themselves. No dependency is named by hand, and an import that cannot be
# resolved fails the script.
#
# Usage:
#   bash scripts/package_windows.sh [build-dir] [dist-dir]
#
# Env: QT_PREFIX (standalone Qt mingw_64 root), MINGW_BIN (msys2 mingw64 bin)

set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="${1:-build}"
DIST="${2:-dist}"
QT_PREFIX="${QT_PREFIX:-C:/Qt/6.10.1/mingw_64}"

# The toolchain that compiled the binaries supplies the C++ runtime and the
# protobuf/abseil DLLs, so locate it rather than hardcoding an install path —
# msys2 does not live in the same place on a workstation and on a CI runner.
# Inside an msys2 shell MINGW_PREFIX names it; otherwise follow g++ on PATH.
if [ -z "${MINGW_BIN:-}" ]; then
    MINGW_BIN="${MINGW_PREFIX:+$MINGW_PREFIX/bin}"
    [ -n "$MINGW_BIN" ] || MINGW_BIN="$(dirname "$(command -v g++)")"
fi

command -v cygpath >/dev/null && QT_PREFIX="$(cygpath -u "$QT_PREFIX")"
command -v cygpath >/dev/null && MINGW_BIN="$(cygpath -u "$MINGW_BIN")"

QT_BIN="$QT_PREFIX/bin"
WINDEPLOYQT="$QT_BIN/windeployqt.exe"

[ -x "$WINDEPLOYQT" ] || { echo "windeployqt not found at $WINDEPLOYQT" >&2; exit 1; }

# --- Application payload -------------------------------------------------

rm -rf "$DIST"
mkdir -p "$DIST"

# The Explorer core and every format backend are linked into the executable.
cp "$BUILD_DIR/automotive-format-explorer.exe" "$DIST/"
cp -r samples "$DIST/samples"

# --- Qt ------------------------------------------------------------------
#
# --no-compiler-runtime: Qt's mingw runtime is older than the toolchain that
# built our binaries. The closure walk below supplies it from msys2 instead.

PATH="$QT_BIN:$PATH" "$WINDEPLOYQT" \
    --qmldir qml \
    --release \
    --no-translations \
    --no-compiler-runtime \
    "$DIST/automotive-format-explorer.exe"

# --- Dependency closure --------------------------------------------------
#
# Every binary under dist/ is walked, including the Qt plugins windeployqt just
# dropped in. msys2 is searched before Qt so the toolchain that compiled these
# binaries supplies the C++ runtime; Qt's own libraries never reach the search,
# they are already in dist/ from windeployqt.

bash scripts/deploy_closure.sh \
    --dest "$DIST" \
    --search "$MINGW_BIN" \
    --search "$QT_BIN" \
    "$DIST"
