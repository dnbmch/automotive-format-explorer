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
# --no-system-d3d-compiler, --no-opengl-sw: Qt Quick renders with Direct3D 11
# here, whose shader compiler D3DCompiler_47.dll is part of Windows since 8.1;
# the software OpenGL renderer is never loaded.

PATH="$QT_BIN:$PATH" "$WINDEPLOYQT" \
    --qmldir qml \
    --release \
    --no-translations \
    --no-compiler-runtime \
    --no-system-d3d-compiler \
    --no-opengl-sw \
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

# --- Licences ------------------------------------------------------------
#
# LICENSE.txt is the application's own licence. THIRD_PARTY_NOTICES.txt names
# every other DLL in dist/ by where it came from, never from a hand-written
# list: a Qt library, plugin or QML module is the same file under QT_PREFIX,
# shipped under the LGPL v3 with the SBOMs of the Qt modules it belongs to; an
# msys2 DLL is listed under the package whose pacman files list names it, with
# that package's licence files. A DLL that is neither fails the package.

cp LICENSE "$DIST/LICENSE.txt"
mkdir -p "$DIST/licenses/qt"

QT_VERSION="$("$QT_BIN/qtpaths.exe" --query QT_VERSION | tr -d '\r')"
MINGW_ROOT="$(dirname "$MINGW_BIN")"                 # .../msys64/mingw64
MINGW_NAME="$(basename "$MINGW_ROOT")"               # mingw64
PACMAN_DB="$(dirname "$MINGW_ROOT")/var/lib/pacman/local"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Same content; cksum, as msys2's base set has no cmp. Qt ships its own
# libstdc++-6.dll and libgcc_s_seh-1.dll, so a name alone does not tell.
same() { [ -f "$2" ] && [ "$(cksum < "$1")" = "$(cksum < "$2")" ]; }

: > "$WORK/qt"
: > "$WORK/msys2"
while IFS= read -r -d '' dll; do
    rel="${dll#"$DIST"/}"
    name="$(basename "$dll")"
    if same "$dll" "$MINGW_BIN/$name" \
            && pkg="$(grep -l -x "$MINGW_NAME/bin/$name" "$PACMAN_DB"/*/files)"; then
        printf '%s %s\n' "$(dirname "$pkg")" "$name" >> "$WORK/msys2"
    elif same "$dll" "$QT_BIN/$rel" || same "$dll" "$QT_PREFIX/plugins/$rel" \
            || same "$dll" "$QT_PREFIX/$rel"; then
        printf '%s\n' "$rel" >> "$WORK/qt"
    elif [ "$rel" != automotive-format-explorer.exe ]; then
        echo "no licence source for $rel" >&2
        exit 1
    fi
done < <(find "$DIST" -type f \( -name '*.dll' -o -name '*.exe' \) -print0)

# Each Qt module's SBOM lists the files it ships: keep those naming a deployed one.
qt_sboms=0
for sbom in "$QT_PREFIX"/sbom/*.spdx; do
    case "$sbom" in *.source.spdx) continue ;; esac
    while IFS= read -r rel; do
        case "$rel" in
            */*) file="./plugins/$rel"; [ "${rel%%/*}" = qml ] && file="./$rel" ;;
            *)   file="./bin/$rel" ;;
        esac
        if grep -q -x -F "FileName: $file" "$sbom"; then
            cp "$sbom" "$DIST/licenses/qt/"
            qt_sboms=$((qt_sboms + 1))
            break
        fi
    done < "$WORK/qt"
done
[ "$qt_sboms" -gt 0 ] || { echo "no Qt SBOM names a deployed file" >&2; exit 1; }

{
    printf 'Automotive Format Explorer is licensed under the GNU GPL v3 (LICENSE.txt).\n'
    printf 'It ships the following third-party software.\n\n'
    printf '== Qt %s ==\n\n' "$QT_VERSION"
    printf 'The Qt libraries, plugins and QML modules in this folder (%s files), by The Qt\n' \
        "$(wc -l < "$WORK/qt")"
    printf 'Company Ltd and other contributors, under the GNU Lesser General Public License v3\n'
    printf '(below; it builds on the GNU GPL v3 in LICENSE.txt). They are unmodified and\n'
    printf 'dynamically linked, and can be replaced with your own build of Qt %s. Source:\n' "$QT_VERSION"
    printf 'https://download.qt.io/official_releases/qt/%s/%s/single/\n' "${QT_VERSION%.*}" "$QT_VERSION"
    printf 'The third-party components inside Qt, with their licences, are listed in the SPDX\n'
    printf 'files under licenses/qt/.\n\n'
    cat resources/licenses/LGPL-3.0.txt
    sort -u -k1,1 "$WORK/msys2" | while read -r pkg _; do
        name="$(sed -n '/^%NAME%$/{n;p;q}' "$pkg/desc")"
        version="$(sed -n '/^%VERSION%$/{n;p;q}' "$pkg/desc")"
        dlls="$(awk -v p="$pkg" '$1 == p {print $2}' "$WORK/msys2" | sort | paste -sd ' ')"
        printf '\n== %s %s: %s ==\n' "$name" "$version" "$dlls"
        grep "^$MINGW_NAME/share/licenses/.*[^/]\$" "$pkg/files" | while read -r licence; do
            printf '\n-- %s --\n\n' "${licence##*/}"
            cat "$(dirname "$MINGW_ROOT")/$licence"
        done
    done
} > "$DIST/THIRD_PARTY_NOTICES.txt"
