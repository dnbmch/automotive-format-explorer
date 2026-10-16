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

### BL-V6: frame payload decoded against the attached database

A bus log's frame table shows each frame's payload (`DataBytes`) and the
structure's own Bytes as hex ([value table](../ref/value_table.md#pages)). A bus
log may attach the database that describes its frames (a DBC for CAN), which the
structure references through `cn_at_reference` (MDF Bus Logging AS §4.3). Wanted:
the payload decoded into the attached database's signals through the DBC parser,
beside the hex. Trigger: the reader reads a structure's attachment references
([mdf4-parser backlog](../../mdf4-parser/docs/backlog.md#a-structures-attachment-references-are-not-read)).

**Size:** M.

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

### BL-K5: a toolchain downgrade can leave a stale DLL in the build tree

`scripts/deploy_closure.sh` refreshes already-deployed files with `cp -u`
(mtime comparison), because a content compare is not available in every
invocation environment (msys2's `base` set ships no `cmp`). A pacman
*downgrade* that restores an older mtime therefore leaves the newer, stale
copy in place for build-tree runs. Packaging is unaffected — `dist/` is
rebuilt from scratch every time. Workaround on a downgrade: delete the
deployed DLLs next to the build-tree executable and rebuild.

**Size:** XS if msys2 ever ships `cmp` in base; otherwise accept.
