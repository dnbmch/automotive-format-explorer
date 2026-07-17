#!/usr/bin/env bash
# Seed build/_parser_deps/ from the sibling parser working trees so the explorer
# configures and builds OFFLINE — no published -lib release needed.
#
# Stages exactly the layout release.yml publishes per parser (headers + raw
# protos merged under include/<fmt>/; static lib + pre-generated pb.h under
# lib/); FetchParserLib's sentinel check then skips every download. NOTE: the
# staged bits are the sibling repos' CURRENT WORKING TREES, not the pinned
# release — the tag in the directory name is only the cache key.
#
#   bash seed-parser-deps.sh      # then: bash build.sh
#
# Each sibling parser must already be built (its build/lib<target>.a exists):
#   bash ../build-all.sh   or   cmake --build <repo>/build
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"
DEPS_DIR="$HERE/build/_parser_deps"

pin() { sed -n "s/^set($1 \"\(v[^\"]*\)\".*/\1/p" "$HERE/CMakeLists.txt"; }

seed_one() { # target repo fmt
  local target="$1" repo="$2" fmt="$3"
  local tag; tag="$(pin "$4")"
  [ -n "$tag" ] || { echo "ERROR: no $4 pin found in CMakeLists.txt"; exit 1; }
  local src="$ROOT/$repo" cache="$DEPS_DIR/$target-$tag"
  local lib="$src/build/lib$target.a"
  [ -f "$lib" ] || { echo "ERROR: $lib not found — build $repo first"; exit 1; }

  rm -rf "$cache"
  mkdir -p "$cache/include" "$cache/lib/$fmt"
  cp -r "$src/lib/include/$fmt" "$cache/include/"
  cp "$src/lib/proto/$fmt"/*.proto "$cache/include/$fmt/"
  cp "$lib" "$cache/lib/"
  cp "$src/build/$fmt"/*.pb.h "$cache/lib/$fmt/"
  echo "  $target-$tag  <-  $repo @ $(git -C "$src" rev-parse --short HEAD)"
}

echo "Seeding _parser_deps from sibling working trees (local HEAD, not the pinned releases):"
seed_one a2lparser a2l-parser a2l A2L_PARSER_VERSION
seed_one dbcparser dbc-parser dbc DBC_PARSER_VERSION
seed_one ldfparser ldf-parser ldf LDF_PARSER_VERSION
echo "Done. Configure/build the explorer normally; the artifact fetch is skipped."
