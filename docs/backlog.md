# Backlog — automotive-format-explorer

Items deliberately deferred or pending. Each entry says **what**, **where**, and the rough size.

## Parked operator decisions

Structural findings from /enforce coverage, awaiting a call — deliberately not applied.

### BL-E2: adapter triplication

`src/adapters/a2ladapter.cpp` / `dbcadapter.cpp` / `ldfadapter.cpp` are ~95% identical (70/70/66 lines). The finder judged a shared abstraction premature at three near-clones; revisit if a fourth format lands or the clones drift.

**Size:** M — only if unified; the status quo is a deliberate keep.
## Packaging / release

### BL-K1: the deployed `qml/` tree is redundant

`windeployqt --qmldir qml` (`scripts/package_windows.sh`) copies QML module
directories that the application never loads. Its QML is compiled into the
binary by qmlcachegen, and the modules it imports self-register from the linked
Qt libraries — the executable's import table names only `Qt6Qml`, `Qt6Quick`,
and `Qt6QuickControls2`. Removing the whole `dist/qml` tree still leaves the app
launching cleanly under the smoke test. Dropping `--qmldir` would shed most of
the packaged file count, but the tree is also what pulls the Controls style and
Dialogs plugins in, and those are reached lazily on user action — paths the
headless smoke test never exercises. Needs a real interactive run with every
dialog opened before the flag is dropped.

**Size:** S to change, M to verify honestly.

### BL-K2: no launch gate on the Linux AppImage

The Windows package is gated by `scripts/smoke_windows.sh`; the AppImage is only
gated by its build succeeding. A `QT_QPA_PLATFORM=offscreen` run with
`APPIMAGE_EXTRACT_AND_RUN=1` would close the gap, but it cannot be validated
from this workstation, and a wrong gate blocks releases on the `publish` job.
Add it when a Linux box is at hand to test against.

**Size:** XS to write, S to validate.

### BL-K3: `DeployMsys2Deps.cmake` still hand-lists DLLs

`cmake/DeployMsys2Deps.cmake` names protobuf, abseil, zlib, and the compiler
runtime explicitly — the same drift exposure that broke the v0.2.0 package,
scoped to build-tree runs rather than the shipped artifact. The closure walk in
`scripts/package_windows.sh` does this without a list; the CMake path could call
the same script instead of maintaining its own.

**Size:** S.
