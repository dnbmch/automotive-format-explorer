# automotive-format-explorer — handoff

## 2026-10-08 — launch check rejects incomplete bundles and failed MDF4 opens — DONE

`--check` opens the sample filenames declared in the built-in format list, including
missing paths. MDF4 retains its diagnostic tab after a failed opening and passes that
opening error through the load result to the sequence. Recoverable diagnostics remain
successful opens. Contracts are in [architecture](arch/architecture.md#opening-several-files),
[adapter contract](arch/adapter_contract.md) and [packaging](ref/release_packaging.md#launch-gates).

The new executable and controller regressions failed before the source fix and pass
after it. Seven focused suites pass on Windows MinGW Debug and Linux GCC Release.
Both package gates pass a complete bundle; missing/corrupt MDF4 fails with exit 1,
as does an explicitly missing MDF4 path. Evidence: workspace `build-check-verdict/`
and srv-one `/tmp/aff-check-verdict-5yicFdZH/workspace/`. Linux used an isolated
checkout and build; `/opt/aff` and the other srv-one session are untouched.
No tag or release is part of this correction. Standing operator checks below remain.

## 2026-10-06 — MDF4 detail labels and opening cancellation — OPEN

Landed: the MDF4 detail panel labels the big-endian data types, the algebraic and
value-range conversion kinds and the formula text; the load contract carries a
cancellation flag that `AppController::shutdown()` sets, and the MDF4 reader observes
it while opening ([adapter contract](arch/adapter_contract.md),
[opening and shutdown](arch/architecture.md#opening-files-and-shutdown)). The text
parsers cannot stop, so shutdown still waits for an A2L, DBC or LDF parse. Every
suite is green locally. OPEN only for the check below.

UNVERIFIED — closing during a large MDF4 opening: open a large `.mf4` and close the
window while the loading indicator shows. Pass: the window closes almost at once.
Fail: it closes only after the opening finishes.

## 2026-09-28 — tab ownership and bounded recording viewer — OPEN

`master` needs mdf4-parser `c98c298` and its public lib `933439f` (the bounded
reader: scan, typed outcomes, limits). All three are pushed and carried by the
[latest Explorer release](https://github.com/dnbmch/automotive-format-explorer/releases/latest).

Contracts: [tabs](arch/architecture.md#tabs), [tree navigation](arch/architecture.md#tree-navigation),
[node keys](arch/architecture.md#node-keys), [MDF4 reads](arch/architecture.md#mdf4-reads),
[adapter contract](arch/adapter_contract.md), [signal plot](ref/signal_plot.md),
[memory view](ref/memory_view.md). Remaining MDF4 scope, including real-recording
acceptance: [plan](plans/mdf4_viewer.md).

Landmines:
- `tst_navpanel` and `tst_signalplotview` load the production QML from a test-time
  `ExplorerApp` module that `tests/CMakeLists.txt` assembles; a new import or
  singleton of `NavPanel.qml` or `SignalPlotView.qml` must be added there.
- The painted-item suites compile their item's source themselves (the items belong to
  the executable, not a library) and run with `QT_QPA_PLATFORM=offscreen`. Pixel
  checks compare 8-bit RGB; a `QColor` keeps finer components than the image stores.
- `tst_appcontroller` and `tst_mdf4documentsession` synchronise on
  `QThreadPool::globalInstance()->waitForDone()`; the session suite's `settle()` runs
  the pool dry repeatedly, because a completion starts the next scan. Unrelated pool
  work left running, or a blocked read, makes that wait cover it too.
- `QPromise` throttles progress callouts itself (about 25 per second), which hides a
  missing worker-side interval in short tests; `progressIsCoalesced` runs over a
  second to tell them apart.
- A `var` property written from C++ (`setProperty`) keeps its value when the QObject
  is destroyed; one assigned in QML reads null. A pending nav panel restore detects a
  destroyed tab by its missing `treeModel`.
- On Windows, `qt_standard_project_setup()` emits every executable, tests included,
  into the build root. Any script CMake or Ninja invokes must pin its own msys
  runtime's tools first ([runtime provenance](ref/cmake_build_system.md#runtime-provenance-on-windows)).

UNVERIFIED — the 16 GB reference laptop: build `build-i2i3/b/measure/` (its
`CMakeLists.txt`) against the landed reader and Explorer and run its scenarios
(`run_measurements.py`) there with the generated 14-hour recordings; the recorded
figures come from a 64 GB host whose page cache held the 10 GB file, and from builds
before the reader's stream-trailer check. Pass = the same order of overview and window
times on a cold first read, cancellation well under 100 ms, peak commit about 75 MiB
with a full window; fail = a peak that grows with the recording, or a close or
selection change that waits for a scan to finish.

UNVERIFIED — operator-visual, one Windows launch of a fresh build, with the four
samples and a large recording (the generated 14-hour files or a real one):
- File > Open lists "Automotive files (*.a2l *.dbc *.ldf *.mf4)", then A2L, DBC, LDF,
  MDF4 and All files, each narrowing the listing. The empty sidebar's four sample
  links open; long tab names elide with "…".
- In each sample, expand categories and entities, select a row and scroll; switch
  between the tabs, also in quick succession: each comes back as left.
- Type a filter, switch away and back: the filter text and filtered tree come back;
  clearing the filter restores the tree from before it, its selection included, also
  when nothing was selected or the selected row sat in a collapsed category.
- Close the current middle tab: its right neighbour is highlighted and shown. Close a
  tab left of the current one: the current plot keeps its zoom, a memory view its
  scroll and selection.
- Show the raw view on an A2L module row, then select other rows and an MDF4 channel:
  the JSON follows the selection. LDF's overview row offers no raw view.
- Closing the window during a large A2L load exits after the parse.
- An A2L whose objects span more than 16 MiB, for example RAM and flash without
  `MEMORY_SEGMENT`s: objects at both ends of the derived segment paint in their
  colors; scrolling or "Go to" reaches a far address and flash-highlights its object;
  clicking an object selects it in the tree; overlaps far into the segment are hatched.
- `demo_recording.mf4`: `speed` plots the sine with zoom, pan, hover and reset; `t`
  shows the master-channel notice. A foreign `.mf4`: numeric channels plot, exotic
  ones say "not plottable".
- Large recording: selecting a channel shows "Reading samples…" with a percentage,
  then the whole recording as vertical extrema columns with no line joining them; the
  header reads "Overview" and the sample count. A channel that fits one window turns
  to "Exact samples" shortly after.
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
  "Incomplete" with the center pane dragged to its narrowest; truncating or appending
  to an open file makes the next channel read "…the file changed since it was opened;
  reopen it".

Fail = a missing or unfiltered dialog entry, a dead sample link, clipped tabs; a tab
showing another tab's expansion or scroll, a lost filter or pre-filter tree, a
highlighted tab other than the one shown, a rebuilt plot, an empty or stale raw view;
an unoccupied cell where an object belongs, a scrollbar that cannot reach the far end,
a click that selects nothing; an empty `speed` plot or an empty plot without
explanation, a line joining overview columns, a hover naming a single sample over the
overview, a stale channel or view shown last, a short file presented as complete, a
stuck progress indicator, a close that hangs; a crash.
