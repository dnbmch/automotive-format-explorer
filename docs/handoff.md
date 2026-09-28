# automotive-format-explorer — handoff

## 2026-09-28 — bounded recording viewer corrective re-review accepted — OPEN

The [parent re-review](../../docs/audit/bounded_recording_viewer_review.md) closes
all five I2/I3 findings and accepts the corrected uncommitted candidate. The parent
independently passed Reader 24/24, Explorer PACKAGE 18/18 and the original session
and production-QML reproducers. The 35-file batch delta from the G2 snapshot and all
122 packet hashes matched. Packet: `build-i2i3-fix/final/packet.md`; parent evidence:
`build-i2i3-parent-rereview/`, both at the workspace root.

G2 stays accepted and uncommitted; land it before I2/I3 when authorized. Nothing was
committed, pushed, tagged or released during re-review. BL-V3 remains explicit
pre-existing detail-notification debt for final consolidation. The reference-laptop,
platform and operator checks below remain open. Earlier performance measurements
have not been repeated on the corrected decoder.

## 2026-09-27 — bounded recording viewer, Explorer side (workspace batch I2/I3) — OPEN

Uncommitted, on top of the uncommitted G2 candidate, for the
[bounded recording viewer](../../docs/plans/bounded_recording_viewer.md); the MDF4
reader side is the same batch's `mdf4-parser` work. The plot consumes worker-built
results of `src/models/plotdata.h`: an overview of at most 4,096 index-aligned bins,
merged pairwise as a scan outgrows them, with one domain decision per result set, and
exact windows of at most 4 Mi samples built from the conservative index cover the bins
give. `SignalPlotModel` installs them immutable, asks for the view's window
(`detailWanted` / `detailRequest()`), holds a window only while it covers the view, and
reports NoSignal, Pending, Empty, Failed, Refused, Overview or Detail, an incomplete
count and progress; `plotseries.h` and the GUI-thread summaries are gone.
`SignalPlotItem` draws bins as unjoined extrema columns and hovers bin ranges or exact
samples with their index. `Mdf4DocumentSession` scans through the reader's
`scan()`/`axis()`: one scan in flight with its cancellation and one pending, obsolete
work cancelled and discarded, progress coalesced to 10 per second, results cached
within a 256 MiB allowance that also holds the scan in flight's reservation, evicting
only results nobody else holds and refusing with the numbers otherwise; the tree is
admitted against a 384 MiB estimate. Contract: [MDF4 reads](arch/architecture.md#mdf4-reads),
[signal plot](ref/signal_plot.md).

Verified against the final reader (`build-i2i3/a/prefix-final`): fresh PACKAGE build
without warnings, 17/17 suites on three runs; new `tst_plotdata`, `tst_mdf4recording`
(a generated 4.5 M-sample irregular recording through the production adapter and the
real reader), the rewritten session and plot model suites. Seven plot mutants and ten
scheduler and cache mutants each fail cases. Strict `-Wshadow -Wconversion
-Wsign-conversion` replay of the touched sources and tests: no warnings; `qmllint` on
`SignalPlotView.qml`: no errors. Measured on the development host (64 GB, Release, warm
page cache) with the reader's generated 14-hour 10 kS/s, unsorted and sparse 64 GiB
recordings: overviews of 504 M samples in 6–14 s, windows of a 1 s view in 3–10 ms and
of 4.15 M samples in 64–159 ms (unsorted: rescanning the prefix, about 0.4 s),
cancellation 0.2–3.1 ms quiet and at most 14 ms under compile load, peaks below 22 MiB
working set and 10 MiB commit with an overview, 86 MiB and 75 MiB with a full window;
the result accounting matches the commit growth ([measured scale](ref/signal_plot.md#measured-scale)).
Evidence: `build-i2i3/b/` at the workspace root (`README.txt`).

UNVERIFIED — the 16 GB reference laptop: the measured figures are from a 64 GB host
whose page cache holds the 10 GB recording; rerun `build-i2i3/b/measure/measure.exe`
(its scenarios in `run_measurements.py`) there. Pass = the same order of overview and
window times on a cold first read, cancellation well under 100 ms, peak commit about
75 MiB with a full window; fail = a peak that grows with the recording, or a close or
selection change that waits for a scan to finish.

Landmines:
- The Explorer needs the bounded reader interface (`scan`, `axis`, typed outcomes).
  The pinned `mdf4-parser-lib` v0.1.0 headers lack it. The current release-download CI
  needs a matching published package and pin; local landing, SOURCE builds and
  selected local-prefix verification do not require publication. Releasing remains
  a separate operator decision.
- `QPromise` throttles progress callouts itself (about 25 per second), which hides a
  missing worker-side interval in short tests; `progressIsCoalesced` runs over a
  second to tell them apart.
- A session test's `settle()` runs the pool dry repeatedly, because a completion
  starts the next scan (an overview's is followed by its window's).
- The model announces state and `detailWanted` as they stand after its earlier
  signals, so an observer's nested change is announced once, by itself.

UNVERIFIED — operator-visual, one Windows launch of a fresh build, with a large
recording (the generated 14-hour files or a real one):
- Selecting a channel shows "Reading samples…" with a percentage, then the whole
  recording as vertical extrema columns with no line joining them; the header reads
  "Overview" and the sample count. A channel that fits one window turns to "Exact
  samples" shortly after.
- Hovering the overview highlights a band and reports a time range, an index range,
  the sample count and min/max; hovering exact samples shows a crosshair with the
  value and "index N".
- Zooming in shows a line through the exact samples, with gaps where values are
  missing; zooming out past about four million samples returns to the overview with
  "zoom in for exact samples". Reset view returns to the whole overview at once.
- Clicking several channels quickly, or zooming repeatedly, settles on the last one
  without showing an intermediate result; closing the tab during a long overview is
  prompt.
- A file whose source ends early shows "Incomplete: N of M samples", and still
  "Incomplete" with the center pane dragged to its narrowest; truncating an open
  file makes the next channel read "…the file changed since it was opened; reopen it".

Fail = a line joining overview columns, a hover naming a single sample over the
overview, a stale channel or view shown last, a short file presented as complete, a
stuck progress indicator, a close that hangs, a crash.

## 2026-09-26 — tab and session ownership (workspace cleanup batch G2) — OPEN

Uncommitted candidate on `9622fa3`, corrected and accepted by the
[parent re-review](../../docs/audit/g2_candidate_review.md); the six landing steps of
the [G2 plan](../../docs/plans/g2_tab_session_ownership.md) are recorded as patches in
`build-g2/steps/` at the workspace root. Every tree row, categories included, carries a
session-local node key, and `TreeModel` resolves keys through a flat key table;
`NodeRegistry` is gone. Each format session maps its keys to typed entity paths and
builds a selection's details with its presenter; the shared `DetailPresenter` base is
gone. `DetailModel` produces raw JSON when the raw view reads it, not on selection.
`DocumentTab` owns a session, its tree filter, the tree navigation and the pre-filter
snapshot; `TabModel` owns the tabs. `AppController` tracks the current tab by identity,
announces an index or session change once per state, destroys a closed tab after its
notifications return, defers switches and closes requested inside a tab model row
transaction by tab identity, and ignores every public action after shutdown. `NavPanel`
saves only a view it has restored, and a restore superseded by a newer switch or filter
does nothing. The painted items drop a destroyed model. Contract:
[tabs](arch/architecture.md#tabs), [tree navigation](arch/architecture.md#tree-navigation),
[node keys](arch/architecture.md#node-keys), [adapter contract](arch/adapter_contract.md).

Verified: standalone PACKAGE build 15/15, no warnings; workspace SOURCE build 237/237.
New suites `tst_documenttab`, `tst_detailmodel`, `tst_navpanel` (the production
`NavPanel.qml` offscreen), `tst_signalgriditem` and `tst_signalplotitem`, and 21 new
controller cases. On `9622fa3`, 17 of those controller cases fail, as do keys on every
A2L, DBC and LDF row, a category key through the filter, the MDF4 close with a read in
flight, the three painted items that outlive their model, and raw JSON serialized only
when read; the other four controller cases hold there too. The old nav panel's losses
(categories on a switch, navigation on a filter clear, the pre-filter snapshot
replaced by the filtered view) are recorded by the audit harness. An offscreen drive
of the production `Main.qml` over the four samples passes 18/18; on `9622fa3` four
checks fail: the hidden raw view holds text and keeps it, the tab bar highlights
index 0 after the current middle tab closes, and a close before the current tab
rebuilds the center view. Fifteen mutants each fail cases, among them a restore that
ignores its generation (rapid A → B → A, and a filter entered while a restore is
pending), a save that walks the rows of the last layout, and the reviewed restore that
selects only a visible row. Strict `-Wshadow -Wconversion -Wsign-conversion` replay:
the touched files carry the 19 warnings they had on `9622fa3`, none new. Five repeated
runs of the eight touched suites clean. Selecting an A2L module row of a 30,000-object file takes
0.01 ms instead of 109 ms; showing its raw view pays the 104 ms serialization.
Evidence: `build-g2/` at the workspace root.

Parent review: [candidate findings](../../docs/audit/g2_candidate_review.md). PACKAGE
rerun 15/15; recorded working/evidence identities match. Clearing a filter did not
restore an empty selection or one under a collapsed ancestor. The corrective pass
selects the saved model index directly (`qml/components/NavPanel.qml`, `_apply()`);
`noSelectionRestoredAfterFilter` and `hiddenSelectionRestoredAfterFilter` fail on the
reviewed restore and pass now, with the other nav panel cases unchanged. The
[tab contract](arch/architecture.md#tabs) states that tab row-transaction observers
must not spin nested event loops; direct synchronous reentry is supported. Verified
after the correction: `tst_navpanel` 14/14 and five repeats, PACKAGE 15/15, workspace
SOURCE 237/237, the `Main.qml` drive 18/18. The final landing patch and the identity
are refreshed.

Landmines:
- `tst_navpanel` loads the production `qml/components/NavPanel.qml` from a test-time
  `ExplorerApp` module that `tests/CMakeLists.txt` assembles (`tests/navpanel/`: qmldir,
  a stand-in `AppController`, the host window); a new NavPanel import or singleton
  must be added there.
- A `var` property written from C++ (`setProperty`) keeps its value when the QObject
  is destroyed; one assigned in QML reads null. A pending restore detects a destroyed
  tab by its missing `treeModel`.
- `TreeView.rows` and `contentY` follow the layout, not the last expand or model
  change; saving and the test host call `forceLayout()` first.
- `TreeView` matches another model's index by row and parent; that is why a
  superseded restore must stay inert ([tree navigation](arch/architecture.md#tree-navigation)).
- A `Q_INVOKABLE` returning a parentless QObject hands it to JavaScript ownership (a
  double delete at exit in the audit harness); tabs reach QML only through properties.

UNVERIFIED — no CI has run on the candidate: Linux and macOS builds, `tst_navpanel`'s
offscreen QML module among them.

UNVERIFIED — operator-visual, the same Windows launch as below:
- Open the four samples. In each, expand categories and entities, select a row and
  scroll; switch between the tabs, also in quick succession: each comes back as left.
- Type a filter, switch away and back: the filter text and filtered tree come back;
  clearing the filter restores the tree from before it, its selection included, also
  when nothing was selected or the selected row sat in a collapsed category.
- Close the current middle tab: its right neighbour is highlighted and shown. Close a
  tab left of the current one: the current plot keeps its zoom.
- Show the raw view on an A2L module row, then select other rows and an MDF4 channel:
  the JSON follows the selection.

Fail = a tab showing another tab's expansion or scroll, a lost filter or pre-filter
tree, a highlighted tab other than the one shown, a rebuilt plot, an empty or stale raw
view, a crash.

Next: parent re-review. On acceptance it lands as the plan's six commits; I2 follows
its envelope review.

## 2026-09-25 — sparse memory view (workspace cleanup batch H) — ACCEPTED

Committed locally as `9622fa3`, "memory view: resolve bytes from sorted object intervals",
after the accepted G1 (`daec46e`) and I1 (`325267e`) commits. The
[parent review](../../docs/audit/h_g2_i2_review.md) accepts the tested multi-GiB scope;
it reran PACKAGE 10/10 and matched all seven recorded source/test hashes. The memory
reference states the UI coordinate limits and candidate-scan cost; no production
correction is required by the review. `MemoryMapModel` keeps the current segment's
objects as sorted intervals with a running maximum of their ends, and
`queryBytes(start, count)` resolves ownership and overlap for any byte range from them:
the row drawn last (highest start address, then document order) owns a shared byte.
`objectAtAddress` and `isOverlap` are that query for one byte. `MemoryGridItem` paints
the visible rows from one tile, hit-tests through the same query, and gives each object
its shade once per segment in address order. The 16 MiB overlap map, color map and
object map and `clampedByteSpan` are gone. Row geometry (`totalRows`, `rowForAddress`,
`contentHeight`, row positions) is 64-bit; object and segment ends saturate at the top
of the address space; objects at one address keep document order. Contract:
[memory view](ref/memory_view.md), [rendering](arch/architecture.md#rendering).

Verified: standalone PACKAGE build against `build-i1-parser-prefix` 10/10, no
warnings; `tst_memorymapmodel` 13 cases, among them a per-byte oracle over 300 random
layouts with straddlers, shared starts and unknown sizes; new `tst_memorygriditem`
3 cases painting offscreen into an image, including clicks; strict
`-Wshadow -Wconversion -Wsign-conversion` replay clean on the four touched sources, as
the two production files were before; five repeated runs of both memory suites clean.
Workspace SOURCE build 232/232. Before the change, `tst_memorygriditem` fails on the
pre-H model and grid: an object 48 MiB into a 64 MiB segment paints unoccupied, and a
3 GiB derived segment's `contentHeight` overflows to −469,761,744. Five mutants each
fail their case: unstable sort, first row wins, no segment clip, wrapping interval
ends, shades assigned per painted range. Evidence: `build-h/` at the workspace root.
G1 and I1 evidence: the [DBC/Explorer](../../docs/audit/dbc_explorer_review.md) and
[Reader](../../docs/audit/mdf4_reader_review.md) reviews.

Landmines:
- `tst_memorygriditem` compiles `src/ui/memorygriditem.cpp` itself (the painted items
  are part of the executable, not a library) and runs with `QT_QPA_PLATFORM=offscreen`.
  Pixel checks compare 8-bit RGB; a `QColor` keeps finer components than the image stores.
- `tst_appcontroller` and `tst_mdf4documentsession` (`settle()`) synchronise on
  `QThreadPool::globalInstance()->waitForDone()`; a test that leaves unrelated pool work
  running, or blocks a read, would make that wait cover it too.
- Closing a session from inside the plot model's reset or series notifications is
  unsupported (a Qt model cannot be destroyed while emitting); a read completion's last
  notification is the busy change.
- Runtime provenance is load-bearing in the ctest `PATH`, the packaging closure order
  and `--search`-before-`--provided` ([build reference](ref/cmake_build_system.md)); any
  script CMake or Ninja invokes must pin its own msys runtime's tools first. On Windows,
  `qt_standard_project_setup()` emits every executable, tests included, into the build
  root.

UNVERIFIED — no CI has run: Linux and macOS builds of the static composition, the MDF4
session and the sparse memory view; the Linux AppImage packaging and launch gate; a
headless Windows launch of the package (BL-K6: the gate can pass on a fatal-error dialog).

UNVERIFIED — operator-visual, one Windows launch of a fresh build:
- File > Open lists "Automotive files (*.a2l *.dbc *.ldf *.mf4)", then A2L, DBC,
  LDF, MDF4 and All files, each narrowing the listing.
- The empty sidebar's four sample links open; long tab names elide with "…".
- `demo_recording.mf4`: `speed` plots the sine with zoom, pan, hover and reset; `t`
  shows the master-channel notice.
- A foreign `.mf4`: numeric channels plot; exotic ones say "not plottable".
- A large recording: clicking several channels quickly settles on the last one
  clicked, with no intermediate channel flashing. After truncating or appending to
  the open file from outside, an unviewed channel reads "Samples could not be read:
  source file changed since it was opened; reload it". Closing the tab while a long
  channel loads may freeze the window until that read returns, then carries on
  without the tab.
- Closing the window during a large A2L load exits after the parse.
- An A2L whose objects span more than 16 MiB, for example RAM and flash without
  `MEMORY_SEGMENT`s: objects at both ends of the derived segment paint in their colors;
  scrolling or "Go to" reaches a far address and flash-highlights its object; clicking
  an object selects it in the tree; overlaps far into the segment are hatched.

Fail = missing or unfiltered entries, a dead sample link, clipped tabs, an empty
`speed` plot or an empty plot without explanation, a stale channel shown last, a
stuck busy veil, a crash, a hang outlasting the parse or read, an unoccupied cell where
an object belongs, a scrollbar that cannot reach the far end, or a click that selects
nothing.

Open: the operator's verdict on a real-world `.mf4`. If one comes back mostly
non-plottable and matters, dump it with the parser's `mdf4_json`, map each
non-decodable channel class to the reader increment that unlocks it (VLSD,
unsorted, arrays, MLSD, bus logging) and spec those increments as a locked plan;
reader breadth is bought, not assumed.
