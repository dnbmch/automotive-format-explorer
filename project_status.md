# automotive-format-explorer — project status

Current state of play. Direction lives in [roadmap.md](roadmap.md); concrete
deferred items in [docs/backlog.md](docs/backlog.md).

## Built

- Qt 6 / QML desktop app: tree view + detail panel + format-specific center view.
- Static per-format backends for A2L, DBC, LDF, and MDF4, linked into one
  executable on every platform and composed from a single built-in format list
  (id, suffixes, adapter) that also derives the dialog filters and sample list;
  each backend provides a `FormatAdapter`, a `DocumentSession` and its presenter.
- `AppController` owns its format list and its one pending load: shutdown stops
  opens, suppresses late completions, asks the load to stop (an MDF4 opening
  stops; an A2L, DBC or LDF parse runs to its end) and joins it before adapters go,
  disposing any undelivered session on the GUI thread; afterwards every public
  action does nothing.
- Each open file is a `DocumentTab` owning its session, tree filter and tree
  navigation. The controller tracks the current tab by identity; closing a tab
  announces the new current tab before destroying the closed one, and tab
  switches or closes requested during a tab-model row change are deferred by tab
  identity. Painted items track their model's lifetime.
- A2L memory-map view and DBC/LDF signal-map view via `QQuickPaintedItem`
  C++ renderers (`MemoryGridItem`, `SignalGridItem`) with FBO scrolling.
- Format-neutral single-channel signal plot (`plotdata`, `SignalPlotModel`,
  `SignalPlotItem`): a worker-built overview of at most 4,096 bins covers every
  sample of a channel, and exact windows of at most 4 Mi samples follow the view;
  bins are painted as unjoined extrema columns, exact samples as a line or, when
  dense, as columns. Hover reports a bin range or an exact sample with its index;
  a source that ended early shows "N of M samples"; zoom, pan and reset.
- MDF4 open indexes metadata once into one retained `mdf4::Reader` shared by the
  tree, detail cards, time axes and every scan. The tree is admitted against a
  384 MiB estimate before it is built. One scan runs at a time with a single
  replaceable pending scan; obsolete scans are cancelled and their results and
  progress discarded. Completed overviews and windows share one 256 MiB allowance
  per session with the scan in flight's reservation, evicting only results nobody
  else holds and refusing, with the numbers, when held results leave no room.
  Empty, short, refused, changed-file and failed scans read differently; the domain
  is the reader's time axis or the sample index; group masters are listed as axis
  channels, not signals. Closing a tab cancels its scan and waits for the reader's
  next checkpoint.
- Memory grid: sparse byte queries remove the 16 MiB coverage cap and support tested
  multi-GiB spans, with overlap hatching (bytes claimed by more than
  one object), click-drag byte-range selection with status readout, and hover
  tooltips with record layout / conversion — see [docs/ref/memory_view.md](docs/ref/memory_view.md).
- Bidirectional selection (tree ↔ detail ↔ center) keyed by session-local node keys,
  which every tree row has.
- Per-tab tree filter (`TreeFilterModel` proxy, `Ctrl+F`) with recursive matching.
  Tab switches and filter clears restore each tab's expansion (categories
  included), current row and scroll; the tab keeps its pre-filter navigation.
- Raw JSON is serialized only when the raw view reads it.
- Bundled samples (one per format, `samples/`, including a writer-authored
  `.mf4` recording) with "open a sample" links in the empty sidebar; provenance
  in `samples/SAMPLES.md`.
- Command line: `automotive-format-explorer [files...]` opens each file as a tab,
  one after another (`OpenSequence`). `--check` opens the named files or every
  bundled sample and exits 0 only when each opened, the QML engine reported no
  warning and the main window drew the last one; the launch gates run it.
- Splash overlay + DWM cloak startup.
- Links the four canonical parser targets, from complete installed packages or
  source workspace composition. GPL-3.0.
- QTest coverage for tree filtering, memory models, the plot builders (bin
  partition up to 2^64, domain decision, exact windows over gaps, repeats and
  nonfinite values, storage), the plot model and painted item, A2L/MDF4 detail
  presenters, MDF4 scan scheduling and cancellation, stale progress and results,
  outcomes, reservation and eviction, tree admission, worker-thread hand-off and
  teardown from and during scans, a generated 4.5 M-sample recording through the
  real reader, the production format list (suffixes, dialog filters, sample
  classification, one bundled sample per format opened through the controller)
  controller load/shutdown lifetimes and the tab contract through a fake adapter,
  tab navigation and pre-filter snapshots, lazy raw JSON, the painted items'
  model lifetimes, and the production nav panel offscreen, registered with
  ctest; the CI app jobs run them once `PARSER_PACKAGE_LOCK` is set. The end-to-end
  writer-file smoke opens and plots the bundled `samples/demo_recording.mf4`
  through the production adapter, and a truncated copy asks for a reload;
  `MDF4_WRITER_SAMPLE` points it at a different recording, and it reports as a
  ctest skip when that resolves to nothing.
- CI (Windows MinGW + Ubuntu, also on `release/**`; the app jobs run only when the
  repository variable `PARSER_PACKAGE_LOCK` is set) + `release.yml` (Windows zip
  + Linux AppImage). Every job builds against Qt 6.10.1, as local development does,
  and both app jobs package and run the launch gate.
- Packaging: Windows by dependency closure (`scripts/package_windows.sh`), Linux as
  an AppImage (`scripts/package_linux.sh`, linuxdeploy pinned) whose executable links
  `libGL.so.1`. Both launch gates run the packaged app's `--check`
  (`scripts/smoke_windows.ps1`; `scripts/smoke_linux.sh` on a virtual X server).
  Release jobs run ctest before packaging and publish only after both platforms pass.
  Proven locally: the Windows gate passes on `dist/` and fails without the platform
  plugin; the workspace builds and tests on Ubuntu 24.04 in the srv-one container,
  where the AppImage's gate passes, and the AppImage's check passes through FUSE on
  srv-one's Ubuntu 26.04 desktop, which has no `libopengl0`. Neither gate has a
  runner proof yet. See
  [docs/ref/release_packaging.md](docs/ref/release_packaging.md).

## In flight

`v0.2.1` is the live release: A2L, DBC, and LDF (v0.2.0 plus the packaging
repair, MDF4 excluded). Its tag preserves the shipped commit and the temporary
release branch is retired. The real Windows download launches self-contained with
no unresolved imports, and its packaged backends open the bundled A2L, DBC, and LDF.
Its Linux AppImage starts only where the `libopengl0` package is installed: its
executable names `libOpenGL.so.0`. `master` links `libGL.so.1`, so the next release
needs no extra package; the README names the workaround meanwhile.

Tab and session ownership and the bounded recording viewer are on `master`; their
platform and operator checks are open in
[docs/handoff.md](docs/handoff.md).

MDF4 ships in no explorer release. The backend needs the reader's bounded interface
(`scan`, `axis`, typed outcomes), which the published `mdf4-parser-lib` v0.1.0 lacks.
Package CI and release jobs build against the complete installed packages pinned in
the `PARSER_PACKAGE_LOCK` repository variable
([build reference](docs/ref/cmake_build_system.md#acquire-complete-installed-packages)),
so they need a complete MDF4 package carrying that interface; source builds and a
local package prefix need no publication. Putting the MDF4 backend in a user's hands
is an explorer release, which is on-demand
([remaining scope](docs/plans/mdf4_viewer.md)).

The bundled screenshots (`docs/screenshot_*.png`) predate the per-tab filter and
sample links; regenerate them when the next release is cut.

## Deferred

Tracked in [docs/backlog.md](docs/backlog.md): the adapter-repetition call
(BL-E2) remains parked because the loaders have distinct boundaries. Memory-
grid view-richness and new-format backends are in [roadmap.md](roadmap.md).
