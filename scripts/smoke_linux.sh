#!/usr/bin/env bash
# Launch the packaged AppImage headless. Staying up is the pass condition.
set -euo pipefail

cd "$(dirname "$0")/.."

DIST="${1:-.}"
SECONDS_UP="${SMOKE_SECONDS:-15}"
shopt -s nullglob
apps=("$DIST"/automotive-format-explorer-*-linux-x86_64.AppImage)
[ "${#apps[@]}" -eq 1 ] || {
    echo "expected one packaged AppImage in $DIST, found ${#apps[@]}" >&2
    exit 1
}
APP="${apps[0]}"
[ -x "$APP" ] || { echo "not executable: $APP" >&2; exit 1; }

export APPIMAGE_EXTRACT_AND_RUN=1
export QT_QPA_PLATFORM=offscreen
export QSG_RHI_BACKEND=software

"$APP" &
pid=$!
sleep "$SECONDS_UP"

if kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    echo "smoke: AppImage stayed up ${SECONDS_UP}s"
    exit 0
fi

rc=0
wait "$pid" || rc=$?
echo "smoke: AppImage exited early with rc=$rc" >&2
exit 1
