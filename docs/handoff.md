# automotive-format-explorer — handoff

## 2026-09-26 — tab and session ownership (workspace cleanup batch G2) — OPEN

Uncommitted candidate on `9622fa3`, corrected after the parent review and awaiting
re-review; the six landing steps of
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
