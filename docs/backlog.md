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

### BL-E5: center-grid view state is not kept per tab

A tab keeps its tree navigation, and each center model keeps its own state (A2L
segment and bytes per row, DBC/LDF message and multiplexer group, MDF4 plot range).
The memory grid's scroll position and object and byte-range selection, and the
signal grid's selected signal, live in the painted items, which the center Loader
builds anew whenever a tab is shown, so they reset on an actual tab switch. Closing
another tab does not rebuild the current view. Keeping them would need the items'
state saved into the tab and restored around the rebuild, for each of the views.

**Size:** S per view; wanted only if the reset proves a real annoyance.

## Adapters and presenters

### BL-E4: DBC adapter declares the extraction entry point by hand

`src/adapters/dbcadapter.cpp` includes `dbc/dbcfile.h` and re-declares
`dbc::extract::extractFile` locally instead of including the public
`dbc/extract.h`, so a signature change in the parser would surface as a link error
rather than a compile error. The A2L adapter already includes its extraction
header directly; do the same here, inside the existing `signals` macro guard.

**Size:** XS.

## Viewer data

### BL-V2: a channel that fits one window is scanned twice

Selecting a channel scans it for its overview; when the whole view fits one exact
window, the plot then asks for that window and the session scans the same range
again. One scan could feed both builders when the stated count fits a window, the
window taking the overview's domain decision when both finish. Wanted only if the
second scan proves noticeable on real recordings; it must stay the same scan and
domain rules, not a second decode path.

**Size:** S.

### BL-V3: ISO 17987 big-endian LIN signals are drawn little-endian

An ISO 17987 LDF may declare big-endian signals (`LIN_sig_byte_order_big_endian`,
parsed as `LdfFile.big_endian_signals`), but `LdfDocumentSession` sets
`bigEndian = false` for every signal it maps, so the signal map lays such signals
out little-endian. Map the flag onto `SignalEntry::bigEndian` after confirming the
standard's big-endian bit numbering against the DBC Motorola layout that
`SignalMapModel::bitPositions()` resolves.

**Size:** S.

### BL-V4: signal-map export and print view

Export the current message layout as PNG or SVG (for documentation), and a
print-friendly view of it (`SignalMapView.qml`, `SignalGridItem`). Neither exists.

**Size:** S.

### BL-V5: an LDF signal-map tooltip shows raw bounds as a physical range

`LdfDocumentSession::buildSignalMap` sets `SignalEntry::minimum` / `maximum` from
the first physical range's `min_raw` / `max_raw`, and `SignalMapModel::signalTooltip`
prints them on the `Physical:` line, where a DBC signal shows its physical bounds.
A signal with factor 0.1 over raw 0..255 shows `[0 .. 255]`. Fix: convert both
bounds as raw × factor + offset; drop the "LIN is always LE" comment with BL-V3.
Reword the LDF sentence of [signal_map.md](ref/signal_map.md) "Hover tooltip" when
this lands.

**Size:** XS.

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

### BL-K2: prove the Linux AppImage launch gate on a runner

`scripts/smoke_linux.sh` runs the AppImage's `--check` on a virtual X server, after
packaging in `ci.yml` and before artifact upload in `release.yml`. It passes in the
srv-one container, an Ubuntu 24.04 image with the release job's packages; close this
item after its first successful Ubuntu workflow run.

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
