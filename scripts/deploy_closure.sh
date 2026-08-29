#!/usr/bin/env bash
#
# Resolve the runtime dependency closure of a set of Windows binaries and copy
# what they import next to them. Nothing is named by hand, and an import that
# resolves nowhere fails the script.
#
# Usage:
#   bash scripts/deploy_closure.sh --dest DIR [--search DIR]... [--provided DIR]... ROOT...
#
#   ROOT        a binary, or a directory scanned recursively for .exe/.dll.
#               Every binary found is walked and counts as already deployed.
#   --dest      directory missing dependencies are copied into.
#   --search    searched for a missing dependency; a hit is copied into --dest
#               and walked in turn. Repeatable, searched in the order given.
#   --provided  satisfies an import without copying it, for a directory the
#               loader reaches on its own. Searched only after every --search,
#               so the toolchain that compiled the binaries keeps ownership of
#               the C++ runtime even where Qt ships a same-named copy.
#
# Both deploy paths share this walk: scripts/package_windows.sh for the shipped
# bundle, cmake/DeployRuntimeDeps.cmake for build-tree runs.

set -euo pipefail

# find/xargs/awk/cp must come from the runtime of the bash running this script.
# A same-named tool from a different msys runtime (Git for Windows ships one)
# re-parses the received command line with its own runtime, glob-expanding
# patterns this script passes quoted. Prepend, so a foreign copy earlier on the
# caller's PATH cannot win; CMake invokes this from a build process whose PATH
# need not carry these tools at all.
PATH="${BASH%/*}${PATH:+:$PATH}"

dest=""
search_dirs=()
provided_dirs=()
roots=()

while [ $# -gt 0 ]; do
    case "$1" in
        --dest)     dest="$2"; shift 2 ;;
        --search)   search_dirs+=("$2"); shift 2 ;;
        --provided) provided_dirs+=("$2"); shift 2 ;;
        *)          roots+=("$1"); shift ;;
    esac
done

if [ -z "$dest" ] || [ ${#roots[@]} -eq 0 ]; then
    echo "usage: deploy_closure.sh --dest DIR [--search DIR]... [--provided DIR]... ROOT..." >&2
    exit 1
fi

# objdump lives in the toolchain that compiled the binaries, which is also the
# first place imports are resolved from.
objdump=objdump
if ! command -v "$objdump" >/dev/null; then
    for d in "${search_dirs[@]}"; do
        if [ -x "$d/objdump.exe" ]; then objdump="$d/objdump.exe"; break; fi
    done
fi
command -v "$objdump" >/dev/null || {
    echo "objdump not on PATH nor in any --search directory" >&2
    exit 1
}

SYSTEM_DIR="/c/Windows/System32"

# Breadth-first over the roots, a whole level per objdump run — process spawns
# dominate this walk, and the build-tree deploy pays them on every build. Each
# import is resolved against what is already deployed, then the system
# directory, then --search, then --provided. Names are keyed lower-case; import
# tables and file names disagree on case.

declare -A have seen
frontier=()

# Failures inside a process substitution are invisible to set -e, and an empty
# frontier would let the walk report success having deployed nothing — so the
# enumeration and each import scan are captured and checked.
listing="$(find "${roots[@]}" -type f \( -iname '*.exe' -o -iname '*.dll' \))" || {
    echo "failed to enumerate binaries under: ${roots[*]}" >&2
    exit 1
}

while IFS= read -r f; do
    [ -n "$f" ] || continue
    name="${f##*/}"
    have["${name,,}"]=1
    frontier+=("$f")
done <<< "$listing"

if [ ${#frontier[@]} -eq 0 ]; then
    echo "no binaries found under: ${roots[*]}" >&2
    exit 1
fi

missing=()

while [ ${#frontier[@]} -gt 0 ]; do
    next=()

    deps="$(printf '%s\0' "${frontier[@]}" | xargs -0 "$objdump" -p 2>/dev/null \
            | awk '/DLL Name:/ {print $3}')" || {
        echo "import scan failed on level: ${frontier[*]}" >&2
        exit 1
    }

    while read -r dep; do
        [ -n "$dep" ] || continue
        key="${dep,,}"
        [ -n "${seen[$key]+x}" ] && continue
        seen["$key"]=1

        [ -n "${have[$key]+x}" ] && continue
        [ -e "$SYSTEM_DIR/$dep" ] && continue
        case "$key" in api-ms-*|ext-ms-*) continue ;; esac

        found=""
        for d in "${search_dirs[@]}"; do
            if [ -e "$d/$dep" ]; then found="$d/$dep"; break; fi
        done

        if [ -n "$found" ]; then
            name="${found##*/}"
            cp -u "$found" "$dest/"
            have["${name,,}"]=1
            next+=("$dest/$name")
            continue
        fi

        for d in "${provided_dirs[@]}"; do
            if [ -e "$d/$dep" ]; then found="$d/$dep"; break; fi
        done
        [ -n "$found" ] || missing+=("$dep")
    done <<< "$deps"

    frontier=("${next[@]}")
done

if [ ${#missing[@]} -gt 0 ]; then
    printf 'unresolved dependency: %s\n' "${missing[@]}" >&2
    echo "closure incomplete: ${#missing[@]} unresolved" >&2
    exit 1
fi

echo "dependency closure complete"
