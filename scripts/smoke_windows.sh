#!/usr/bin/env bash
#
# Launch the packaged app headless and fail if it cannot come up.
#
# A missing DLL fails the loader before main(); a root object that will not
# instantiate reaches objectCreationFailed in src/main.cpp and exits -1. Either
# way the process is gone before the timeout. Staying up is the pass condition.
#
# This covers what the closure walk cannot: that the deployed Qt actually
# initialises a QML engine and builds the window, not merely that every import
# resolves.
#
# Usage:
#   bash scripts/smoke_windows.sh [dist-dir]

set -euo pipefail

cd "$(dirname "$0")/.."

DIST="${1:-dist}"
SECONDS_UP="${SMOKE_SECONDS:-15}"
APP="$DIST/automotive-format-explorer.exe"

[ -x "$APP" ] || { echo "not found: $APP" >&2; exit 1; }

export QT_QPA_PLATFORM=offscreen
export QSG_RHI_BACKEND=software

"$APP" &
pid=$!

sleep "$SECONDS_UP"

if kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    echo "smoke: app stayed up ${SECONDS_UP}s"
    exit 0
fi

rc=0
wait "$pid" || rc=$?
echo "smoke: app exited early with rc=$rc" >&2
exit 1
