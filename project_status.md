# automotive-format-explorer — project status

Current state of play. Direction lives in [roadmap.md](roadmap.md); concrete
deferred items in [docs/backlog.md](docs/backlog.md).

## Built

- Qt 6 / QML desktop app: tree view + detail panel + format-specific center view.
- Static per-format backends for A2L, DBC, LDF, and MDF4, linked into one
  executable on every platform and composed from a single built-in format list
  (id, suffixes, adapter) that also derives the dialog filters and sample list;
  each backend provides `FormatAdapter` / `DocumentSession` / `DetailPresenter`.
- `AppController` owns its format list and its one pending load: shutdown stops
  opens, suppresses late completions and joins the load before adapters go,
  disposing any undelivered session on the GUI thread.
- A2L memory-map view and DBC/LDF signal-map view via `QQuickPaintedItem`
  C++ renderers (`MemoryGridItem`, `SignalGridItem`) with FBO scrolling.
- Format-neutral single-channel signal plot (`PlotSeries`, `SignalPlotModel`,
  `SignalPlotItem`) with summary-backed min/max bucketing, zoom, pan, and
  nearest-sample cursor readout.
- MDF4 metadata-only open, channel-group/channel detail cards, and lazy explicit-
  range decode on a worker. Completed decodes land in a byte-budget LRU cache
  shared with the plot model; a result reaches the plot only while its channel
  is still selected. Non-monotonic domains fall back to record indices at the
  session seam; group masters are listed as axis channels, not signals.
- Memory grid: overlap hatching (bytes claimed by more than one object),
  click-drag byte-range selection with status readout, and hover tooltips with
  record layout / conversion — see [docs/ref/memory_view.md](docs/ref/memory_view.md).
- Bidirectional selection (tree ↔ detail ↔ center) keyed by `NodeRegistry`.
- Per-tab tree filter (`TreeFilterModel` proxy, `Ctrl+F`) with recursive matching
  and pre-filter state restore.
- Bundled samples (one per format, `samples/`, including a writer-authored
  `.mf4` recording) with "open a sample" links in the empty sidebar; provenance
  in `samples/SAMPLES.md`.
- Splash overlay + DWM cloak startup.
- Links the four canonical parser targets, from complete installed packages or
  source workspace composition. GPL-3.0.
- QTest coverage for tree filtering, memory and signal-plot models, A2L/MDF4
  detail presenters, MDF4 ranged-decode, cache eviction, domain validation and
  race behavior, the production format list (suffixes, dialog filters, sample
  classification, one bundled sample per format opened through the controller)
  and controller load/shutdown lifetimes through a fake adapter, registered with
  ctest and run in CI. The end-to-end
  writer-file smoke runs against the bundled `samples/demo_recording.mf4`;
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
