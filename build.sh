#!/usr/bin/env bash
# automotive-format-explorer CMake build (Qt6 + protobuf, no tests).
# Compiler + protobuf come from the msys2 mingw64 toolchain; Qt6 comes from a
# standalone Qt install (msys2 does not ship qt6 here). Parser libraries are
# fetched from GitHub releases at configure time, so the first configure needs
# network access.
#
#   bash build.sh                 # Debug build
#   BUILD_TYPE=Release bash build.sh
#   QT_PREFIX=C:/Qt/6.8.0/mingw_64 bash build.sh
set -euo pipefail

MINGW_BIN="${MINGW_BIN:-/c/msys64/mingw64/bin}"
QT_PREFIX="${QT_PREFIX:-C:/Qt/6.10.1/mingw_64}"
export PATH="$MINGW_BIN:$PATH"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT/build"
BUILD_TYPE="${BUILD_TYPE:-Debug}"

cmake -B "$BUILD_DIR" -G Ninja -S "$ROOT" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX"
cmake --build "$BUILD_DIR"
