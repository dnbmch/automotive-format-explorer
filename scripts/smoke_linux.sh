#!/usr/bin/env bash
# Run the packaged AppImage's own check and pass on its verdict.
#
# `automotive-format-explorer --check` opens every bundled sample in turn and
# exits 0 only when each opened, the QML engine reported no warning and the main
# window drew the last one (src/main.cpp). It runs as users run it: on the
# AppImage's xcb platform plugin and Qt's default OpenGL scene graph, here on a
# virtual X server with Mesa's software rasterizer. The AppImage runs extracted,
# so the host needs no FUSE; SMOKE_SECONDS bounds the run.
set -euo pipefail

cd "$(dirname "$0")/.."

DIST="${1:-.}"
SECONDS_MAX="${SMOKE_SECONDS:-60}"
shopt -s nullglob
apps=("$DIST"/automotive-format-explorer-*-linux-x86_64.AppImage)
[ "${#apps[@]}" -eq 1 ] || {
    echo "expected one packaged AppImage in $DIST, found ${#apps[@]}" >&2
    exit 1
}
APP="${apps[0]}"
[ -x "$APP" ] || { echo "not executable: $APP" >&2; exit 1; }

export APPIMAGE_EXTRACT_AND_RUN=1 LC_ALL=C.UTF-8
unset QT_QPA_PLATFORM QT_QUICK_BACKEND

rc=0
timeout --kill-after=10 "$SECONDS_MAX" \
    xvfb-run --auto-servernum --server-args="-screen 0 1280x800x24" "$APP" --check || rc=$?
case $rc in
    0)   echo "smoke: the AppImage's check passed" ;;
    124) echo "smoke: no verdict within ${SECONDS_MAX}s" >&2 ;;
    *)   echo "smoke: the AppImage's check failed with exit $rc" >&2 ;;
esac
exit "$rc"
