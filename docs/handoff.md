# automotive-format-explorer — handoff

## 2026-10-08 — command-line files, `--check`, both launch gates, the Linux package — OPEN

Landed: files named on the command line open as tabs (`OpenSequence`); `--check` opens
them or every bundled sample and exits on its verdict
([opening several files](arch/architecture.md#opening-several-files)); both launch
gates run it; `scripts/package_linux.sh` is the one Linux packaging path, on Qt 6.10.1
like Windows, and the executable links `libGL.so.1`
([release packaging](ref/release_packaging.md)). The check found a live QML warning:
the filter shortcut bound one of `StandardKey.Find`'s keys; it is `Ctrl+F`, as
documented. Proof: Windows suites green, the gate passes on `dist/` and fails without
`platforms/qwindows.dll`; on srv-one every workspace suite passes on Ubuntu 24.04,
the AppImage's gate passes on a virtual X server, and its check passes through FUSE on
the box's Ubuntu 26.04 desktop, which has neither `libopengl0` nor `libfuse2`.

UNVERIFIED — the AppImage by eye: over remote desktop (`mstsc` to
`dnbm-srv-one.tail4bd2e4.ts.net`), start "Automotive Format Explorer (dev)" from the
application grid, open the four samples from the sidebar and click through the A2L
memory map, the DBC and LDF signal maps and the MDF4 `speed` plot with zoom and pan.
Pass: readable text and every view draws. Fail: no window, garbled rendering, a view
that stays empty, a crash.

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
reader: scan, typed outcomes, limits). All three are pushed; no release carries them.

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

UNVERIFIED — CI runners: no runner run of the package-mode app builds or of either
launch gate. On a runner the Windows gate needs an interactive desktop and Windows
PowerShell under the step's `pwsh` shell; the Linux gate (BL-K2) needs `xvfb-run`.
Package CI first needs a complete MDF4 package with the bounded interface in
`PARSER_PACKAGE_LOCK`. The source-mode Linux build and every suite, the AppImage and
its gate pass in the srv-one container (2026-10-08 entry). Pass = app build, ctest
and the launch gate green on both CI jobs, each gate reporting "check passed"; fail
= any configure, build or test failure, or a gate reporting a failed check, an
unexpected window or no verdict.

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
