# automotive-format-explorer — project status

Current state of play. Direction lives in [roadmap.md](roadmap.md); concrete
deferred items in [docs/backlog.md](docs/backlog.md).

## Built

- Qt 6 / QML desktop app: tree view + detail panel + format-specific center view.
- Plugin-per-format backends (shared `.dll` on Windows via `QLibrary`, static on
  Linux) for A2L, DBC, LDF, and MDF4, each providing `FormatAdapter` /
  `DocumentSession` / `DetailPresenter`.
- A2L memory-map view and DBC/LDF signal-map view via `QQuickPaintedItem`
  C++ renderers (`MemoryGridItem`, `SignalGridItem`) with FBO scrolling.
- Format-neutral single-channel signal plot (`PlotSeries`, `SignalPlotModel`,
  `SignalPlotItem`) with summary-backed min/max bucketing, zoom, pan, and
  nearest-sample cursor readout.
- MDF4 metadata-only open, channel-group/channel detail cards, and lazy explicit-
  range decode on a worker. Completed channels are cached and late results from
  stale selections are discarded.
- Memory grid: overlap hatching (bytes claimed by more than one object),
  click-drag byte-range selection with status readout, and hover tooltips with
  record layout / conversion — see [docs/ref/memory_view.md](docs/ref/memory_view.md).
- Bidirectional selection (tree ↔ detail ↔ center) keyed by `NodeRegistry`.
- Per-tab tree filter (`TreeFilterModel` proxy, `Ctrl+F`) with recursive matching
  and pre-filter state restore.
- Bundled samples (one per text format, `samples/`) with "open a sample" links in the
  empty sidebar; provenance in `samples/SAMPLES.md`.
- Splash overlay + DWM cloak startup.
- Links the four parser libraries — fetched from GitHub releases at configure
  time, or staged from the sibling working trees by `seed-parser-deps.sh`. GPL-3.0.
- QTest coverage for tree filtering, memory and signal-plot models, A2L/MDF4
  detail presenters, and MDF4 ranged-decode/cache/race behavior, registered
  with ctest and run in CI.
- CI (Windows MinGW + Ubuntu, also on `release/**`) + `release.yml` (Windows zip
  + Linux AppImage). Windows CI and release build against the same standalone Qt
  as local development.
- Windows packaging by dependency closure (`scripts/package_windows.sh`) plus headless
  Windows and Linux AppImage launch gates (`scripts/smoke_windows.sh`,
  `scripts/smoke_linux.sh`). Release jobs run ctest before packaging and publish only
  after both platforms pass. The Linux gate is locally syntax-checked and awaits its
  first runner proof. See
  [docs/ref/cmake_build_system.md](docs/ref/cmake_build_system.md).

## In flight

`v0.2.1` is the live release: A2L, DBC, and LDF (v0.2.0 plus the packaging
repair, MDF4 excluded). Its tag preserves the shipped commit and the temporary
release branch is retired. The real Windows download launches self-contained with
no unresolved imports, and its packaged backends open the bundled A2L, DBC, and LDF.

MDF4 ships in no explorer release. The private `dnbmch/mdf4-parser` and public
artifact-only `dnbmch/mdf4-parser-lib` repositories now exist and their hardened
`main` branches are pushed. Publication is blocked only on a fine-grained
`LIB_RELEASE_TOKEN` for the source workflow; after v0.1.0 assets exist, explorer
must pin their headers sha256 and rerun current `master` CI. Run `30772666463`
corresponds to current master and fails on both platforms only at the expected
v0.1.0 headers fetch; no build/test result exists past that gate yet.

The bundled screenshots (`docs/screenshot_*.png`) predate the per-tab filter and
sample links; regenerate them when the next release is cut.

## Deferred

Tracked in [docs/backlog.md](docs/backlog.md): the adapter-repetition call
(BL-E2) remains parked because the loaders have distinct boundaries. Memory-
grid view-richness and new-format backends are in [roadmap.md](roadmap.md).
