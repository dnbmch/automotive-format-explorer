#!/usr/bin/env bash
#
# Package the Windows build into a self-contained dist/ directory.
#
# Qt is deployed with windeployqt; everything else is resolved by walking the
# dependency closure of the binaries themselves. No dependency is named by
# hand, and an import that cannot be resolved fails the script.
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
SYSTEM_DIR="/c/Windows/System32"
WINDEPLOYQT="$QT_BIN/windeployqt.exe"

command -v objdump >/dev/null || { echo "objdump not on PATH" >&2; exit 1; }
[ -x "$WINDEPLOYQT" ] || { echo "windeployqt not found at $WINDEPLOYQT" >&2; exit 1; }

lower() { tr 'A-Z' 'a-z'; }

# --- Application payload -------------------------------------------------

rm -rf "$DIST"
mkdir -p "$DIST"

cp "$BUILD_DIR/automotive-format-explorer.exe" "$DIST/"
cp "$BUILD_DIR/explorer-core.dll" "$DIST/"

backends=("$BUILD_DIR"/explorer-*-backend.dll)
[ -e "${backends[0]}" ] || { echo "no backend DLLs in $BUILD_DIR" >&2; exit 1; }
cp "${backends[@]}" "$DIST/"
echo "backends packaged: ${#backends[@]}"

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
# Breadth-first over every binary under dist/. Each import is resolved against
# what is already packaged, then the system directory, then msys2 and Qt.
#
# msys2 is searched before Qt so the toolchain that compiled these binaries
# supplies the C++ runtime. Qt's own libraries never reach this search — they
# are already in dist/ from windeployqt.

search_dirs=("$MINGW_BIN" "$QT_BIN")

declare -A have seen
queue=()

while IFS= read -r f; do
    have["$(basename "$f" | lower)"]=1
    queue+=("$f")
done < <(find "$DIST" -type f \( -iname '*.exe' -o -iname '*.dll' \))

missing=()

while [ ${#queue[@]} -gt 0 ]; do
    bin="${queue[0]}"
    queue=("${queue[@]:1}")

    while read -r dep; do
        key="$(printf '%s' "$dep" | lower)"
        [ -n "${seen[$key]+x}" ] && continue
        seen["$key"]=1

        [ -n "${have[$key]+x}" ] && continue
        [ -e "$SYSTEM_DIR/$dep" ] && continue
        case "$key" in api-ms-*|ext-ms-*) continue ;; esac

        found=""
        for d in "${search_dirs[@]}"; do
            [ -e "$d/$dep" ] && { found="$d/$dep"; break; }
        done

        if [ -n "$found" ]; then
            cp "$found" "$DIST/"
            name="$(basename "$found")"
            have["$(printf '%s' "$name" | lower)"]=1
            queue+=("$DIST/$name")
        else
            missing+=("$dep")
        fi
    done < <(objdump -p "$bin" 2>/dev/null | awk '/DLL Name:/ {print $3}')
done

if [ ${#missing[@]} -gt 0 ]; then
    printf 'unresolved dependency: %s\n' "${missing[@]}" >&2
    echo "packaging failed: ${#missing[@]} unresolved" >&2
    exit 1
fi

echo "dependency closure complete"
