# Backlog — automotive-format-explorer

Items deliberately deferred or pending. Each entry says **what**, **where**, and the rough size.

## Parked operator decisions

Structural findings from /enforce coverage, awaiting a call — deliberately not applied.

### BL-E2: adapter repetition

The four adapters repeat protobuf-diagnostic translation; format identity and
construction live once, in the built-in format list. Their actual load boundaries differ: A2L/DBC/LDF own text-loader
objects while MDF4 opens one retained reader and hands its metadata and reads to
the session. A generic load template would hide that distinction for little source
reduction. Keep the explicit adapters; extract only diagnostic translation if
its semantics ever need to change together.

**Size:** S for the diagnostic helper; the explicit load flows are a deliberate keep.

### BL-E3: mdf4 presenter labels for the new proto enum values

The sibling `mdf4-parser` proto gained `DataType` `UINT_BE`/`SINT_BE`/`FLOAT_BE`
and `ConversionKind` `ALGEBRAIC`/`TAB_RANGE` (plus `Conversion.formula`). The
`mdf4detailpresenter.cpp` switches don't name them and fall through to their
`"Unknown (%1)"` default — correct but unlabeled. The cases cannot be added while
the explorer compiles against the published v0.1.0 `-lib` artifacts, which
predate the enum values; add the labels (and optionally show the formula text)
in the same batch as the next parser `-lib` release pickup.

**Size:** XS, gated on a fresh `mdf4-parser-lib` release.

### BL-E4: DBC adapter declares the extraction entry point by hand

`src/adapters/dbcadapter.cpp` includes `dbc/dbcfile.h` and re-declares
`dbc::extract::extractFile` locally instead of including the public
`dbc/extract.h`, so a signature change in the parser would surface as a link error
rather than a compile error. The A2L adapter already includes its extraction
header directly; do the same here, inside the existing `signals` macro guard.

**Size:** XS.

### BL-E5: center-grid view state is not kept per tab

A tab keeps its tree navigation, and each center model keeps its own state (A2L
segment and bytes per row, DBC/LDF message and multiplexer group, MDF4 plot range).
The memory grid's scroll position and object and byte-range selection, and the
signal grid's selected signal, live in the painted items, which the center Loader
builds anew whenever a tab is shown, so they reset on an actual tab switch. Closing
another tab does not rebuild the current view. Keeping them would need the items'
state saved into the tab and restored around the rebuild, for each of the views.

**Size:** S per view; wanted only if the reset proves a real annoyance.

## Viewer data

### BL-V1: MDF4 opening cannot be cancelled

`Mdf4Adapter::load()` constructs its `mdf4::Reader` without a cancellation flag,
although the reader stops an opening when one is set. The load contract has none:
`AppController::shutdown()` waits for a pending load, so closing the application
while a large MDF4 file opens waits for its opening (bounded by the reader's
`openingBytes`). Threading a flag from the controller through `FormatAdapter::load()`
would make shutdown prompt for MDF4; the text formats would ignore it.

**Size:** S.

### BL-V2: a channel that fits one window is scanned twice

Selecting a channel scans it for its overview; when the whole view fits one exact
window, the plot then asks for that window and the session scans the same range
again. One scan could feed both builders when the stated count fits a window, the
window taking the overview's domain decision when both finish. Wanted only if the
second scan proves noticeable on real recordings; it must stay the same scan and
domain rules, not a second decode path.

**Size:** S.

### BL-V3: an MDF4 selection notifies the detail panel before its plot flow

`Mdf4DocumentSession::selectNode()` sets the detail model's selection, which
notifies, and then runs the plot flow with its local path and no lifetime check. An
observer of the detail model that selects another row or closes the tab would leave
that flow on a superseded selection or a destroyed session. No such observer exists.
The fix is the session's own rule: check the session's lifetime after the detail
notification (or settle the plot state first), and cover it with a reentry test.

**Size:** S.

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

### BL-K2: prove the new Linux AppImage launch gate on a runner

`scripts/smoke_linux.sh` now runs the AppImage offscreen with
`APPIMAGE_EXTRACT_AND_RUN=1`, and `release.yml` places it before artifact upload.
The script is syntax-checked locally; close this item after its first successful
Ubuntu release-workflow run.

**Size:** XS verification.

### BL-K5: a toolchain downgrade can leave a stale DLL in the build tree

`scripts/deploy_closure.sh` refreshes already-deployed files with `cp -u`
(mtime comparison), because a content compare is not available in every
invocation environment (msys2's `base` set ships no `cmp`). A pacman
*downgrade* that restores an older mtime therefore leaves the newer, stale
copy in place for build-tree runs. Packaging is unaffected — `dist/` is
rebuilt from scratch every time. Workaround on a downgrade: delete the
deployed DLLs next to the build-tree executable and rebuild.

**Size:** XS if msys2 ever ships `cmp` in base; otherwise accept.

### BL-K6: the Windows launch gate can pass on a fatal-error dialog

`scripts/smoke_windows.sh` forces `QT_QPA_PLATFORM=offscreen`, but `windeployqt`
deploys only `platforms/qwindows.dll`. Unless the environment points Qt at its own
plugin directory — CI runs after `install-qt-action`, whose default environment
setup is expected to, though no runner log was checked — the packaged app cannot
create its platform and hits Qt's fatal error. Depending on
how it is launched, that either fails fast (`0xC0000602`) or leaves a
"automotive-format-explorer" error box up, which survives `SMOKE_SECONDS` and
passes. Reproduced locally; the release_packaging.md claim that
every startup fault exits before the timeout does not hold for this case. Decide
between deploying the offscreen plugin and running the gate without CI's Qt
environment, or asserting the real window instead of survival.

**Size:** S.
