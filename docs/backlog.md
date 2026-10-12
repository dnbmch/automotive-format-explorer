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

### BL-V5: a text channel's values are not shown

A text channel (`Channel.textual`) is listed and described, but its samples are
never read: selecting it shows "Text channel — not plottable" and scans nothing
(`Mdf4DocumentSession::selectChannel()`). A value table, one row per sample with its
index, time and text, would read through the reader's text lane (`scanText`, the
mdf4-parser string-channel increment), bound by `Mdf4Adapter::load()` beside `scan`,
into a windowed list model the session owns and accounts within its result
allowance, shown in the center panel in place of the plot. The center panel's source
and model are one per session (`DocumentSession::centerPanelSource()`, announced only
by `AppController::currentSessionChanged`), so either the selection switches the
center view or one MDF4 center view hosts both the plot and the table. The plot stack
is untouched.

**Size:** M.

### BL-V6: an array channel's values are not shown

An array channel (`Channel.array`) is listed with its type and shape and described
with its axes, but its values are never read: selecting it shows "Array channel — not
plottable" and scans nothing (`Mdf4DocumentSession::selectChannel()`). A one-sample
table in the center panel would read through the reader's array lane,
`readArray(g, c, s, 1, window)` (the mdf4-parser channel-array increment), bound by
`Mdf4Adapter::load()` beside `scan`, on a worker shaped like `launch()`, into an
`ArrayTableModel`: a `QAbstractListModel` with one row per matrix row and a `cells`
role (the center-panel contract, `DocumentSession::centerPanelModel()`), the axes as
headers (an interval axis's borders as `[a, b[` labels, `]a, b]` when left-open), a
sample selector, slice selectors for more than two dimensions, NaN cells blank, the
matrix retained at 8 B per value within the session's result allowance. The center-view
switch and the retained-result variant are BL-V5's, shared.

Its own fork: which axis-channel sample labels array sample s. Recommended: the spec's
rule (ASAM MDF 4.2.0 base standard p.118, p.115, p.128-129) — a one-cycle axis group
without a master labels every sample (the classification results of
`Arrays/Classification/Porsche_2D_classification_result.mf4`,
`ClassificationResults/2DClassification.mf4` and `ClassificationResults/Rainflow.mf4`),
otherwise the axis sample with the latest common-master value at or before the array
sample's, index headers before the first. The same record index (asammdf 8.8.18's
rule, `mdf_v4.py:8129-8140`) only as a labelled non-spec fallback: on
`Arrays/Simple/Vector_MeasurementArrays.mf4` it labels `KF2` sample s with the `Curve2`
sample recorded 0.14 s after it.

Check list, files under the 4.2.0 corpus's `ASAM_MDF_BS_V4-2-0/BaseStandard/Examples/`:
`2D_classification` of `Arrays/Classification/Porsche_2D_classification_result.mf4`,
52 × 142 with `throttle` / `nmot` interval headers; `KF4` of
`Arrays/Simple/Vector_MeasurementArrays.mf4` with a fixed and a channel axis;
`Signal_MatrixDim` of `Arrays/Simple/dSPACE_MeasurementArrays.mf4` with a slice
selector.

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
