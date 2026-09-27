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
  opens, suppresses late completions and joins the load before adapters go,
  disposing any undelivered session on the GUI thread; afterwards every public
  action does nothing.
- Each open file is a `DocumentTab` owning its session, tree filter and tree
  navigation. The controller tracks the current tab by identity; closing a tab
  announces the new current tab before destroying the closed one, and tab
  switches or closes requested during a tab-model row change are deferred by tab
  identity. Painted items track their model's lifetime.
- A2L memory-map view and DBC/LDF signal-map view via `QQuickPaintedItem`
  C++ renderers (`MemoryGridItem`, `SignalGridItem`) with FBO scrolling.
- Format-neutral single-channel signal plot (`PlotSeries`, `SignalPlotModel`,
  `SignalPlotItem`) with summary-backed min/max bucketing, zoom, pan, and
  nearest-sample cursor readout.
- MDF4 open indexes metadata once into one retained `mdf4::Reader` shared by the
  tree, detail cards and every channel read. Channel reads run on a worker one at
  a time, with a single replaceable pending selection; successful reads land in a
  byte-budget LRU cache shared with the plot model and reach the plot only while
  their channel is still selected. A failed read shows its reason (a changed file
  asks for a reload) and is not cached; an empty success shows no samples. Closing
  the tab waits for a running read. Non-monotonic domains fall back to record
  indices at the session seam; group masters are listed as axis channels, not
  signals.
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
- Splash overlay + DWM cloak startup.
- Links the four canonical parser targets, from complete installed packages or
  source workspace composition. GPL-3.0.
- QTest coverage for tree filtering, memory and signal-plot models, A2L/MDF4
  detail presenters, MDF4 serialized reads, stale completions, failure versus
  empty results, cache eviction, domain validation, worker-thread hand-off and
  teardown from and during reads, the production format list (suffixes, dialog filters, sample
  classification, one bundled sample per format opened through the controller)
  controller load/shutdown lifetimes and the tab contract through a fake adapter,
  tab navigation and pre-filter snapshots, lazy raw JSON, the painted items'
  model lifetimes, and the production nav panel offscreen, registered with
  ctest and run in CI. The end-to-end
  writer-file smoke opens and plots the bundled `samples/demo_recording.mf4`
  through the production adapter, and a truncated copy asks for a reload;
  `MDF4_WRITER_SAMPLE` points it at a different recording, and it reports as a
  ctest skip when that resolves to nothing.
- CI (Windows MinGW + Ubuntu, also on `release/**`) + `release.yml` (Windows zip
  + Linux AppImage). Windows CI and release build against the same standalone Qt
  as local development.
- Windows packaging by dependency closure (`scripts/package_windows.sh`) plus headless
  Windows and Linux AppImage launch gates (`scripts/smoke_windows.sh`,
  `scripts/smoke_linux.sh`). Release jobs run ctest before packaging and publish only
  after both platforms pass. The Linux gate is locally syntax-checked and awaits its
  first runner proof. See
  [docs/ref/release_packaging.md](docs/ref/release_packaging.md).

## In flight

`v0.2.1` is the live release: A2L, DBC, and LDF (v0.2.0 plus the packaging
repair, MDF4 excluded). Its tag preserves the shipped commit and the temporary
release branch is retired. The real Windows download launches self-contained with
no unresolved imports, and its packaged backends open the bundled A2L, DBC, and LDF.

MDF4 ships in no explorer release. Its parser artifacts are published as
`mdf4-parser-lib` v0.1.0, and `master` fetches the headers archive under a sha256
pin like the other three parsers. Putting the MDF4 backend in a user's hands is an
explorer release, which is on-demand.

The bundled screenshots (`docs/screenshot_*.png`) predate the per-tab filter and
sample links; regenerate them when the next release is cut.

## Deferred

Tracked in [docs/backlog.md](docs/backlog.md): the adapter-repetition call
(BL-E2) remains parked because the loaders have distinct boundaries. Memory-
grid view-richness and new-format backends are in [roadmap.md](roadmap.md).
