#!/usr/bin/env bash
# Standalone package build. PARSER_PREFIX is a complete installed parser prefix.
# Workspace source builds: bash ../build-all.sh
set -euo pipefail
SCRIPT_DIR="${BASH_SOURCE[0]%/*}"
if [[ "$SCRIPT_DIR" == "${BASH_SOURCE[0]}" ]]; then SCRIPT_DIR=.; fi
ROOT="$(cd "$SCRIPT_DIR" && pwd)"
: "${PARSER_PREFIX:?Set PARSER_PREFIX to the complete installed parser packages}"
MINGW_BIN="${MINGW_BIN:-/c/msys64/mingw64/bin}"
CMAKE_BIN="${CMAKE_BIN:-/c/Qt/Tools/CMake_64/bin}"
QT_PREFIX="${QT_PREFIX:-C:/Qt/6.10.1/mingw_64}"
export PATH="$CMAKE_BIN:$MINGW_BIN:$PATH"
cmake -S "$ROOT" -B "${BUILD_DIR:-$ROOT/build-package}" -G Ninja \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Debug}" -DAFF_PARSER_MODE=PACKAGE \
  -DCMAKE_PREFIX_PATH="$PARSER_PREFIX;${PARSER_TOOLCHAIN_PREFIX:-C:/msys64/mingw64};$QT_PREFIX"
cmake --build "${BUILD_DIR:-$ROOT/build-package}" --parallel "${BUILD_JOBS:-4}"
