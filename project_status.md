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
- Windows packaging by dependency closure (`scripts/package_windows.sh`) plus a
  headless launch gate (`scripts/smoke_windows.sh`); the release publishes only
  after every platform builds, packages, and launches. See
  [docs/ref/cmake_build_system.md](docs/ref/cmake_build_system.md).

## In flight

`v0.2.1` is the live release: A2L, DBC, and LDF, cut from `release/v0.2.1`
(v0.2.0 plus the packaging repair, MDF4 excluded). It is verified to launch
self-contained with no unresolved imports.

MDF4 ships in no release and cannot build anywhere but a workstation: the
backend is built and audited, but `dnbmch/mdf4-parser` and `dnbmch/mdf4-parser-lib`
do not exist, so the pinned artifact fetch fails and `master` CI is red at
Configure. Creating those repos and cutting `mdf4-parser` v0.1.0 unblocks both
CI and an MDF4-carrying release; the source repo stays private like its siblings,
only the `-lib` artifacts repo is public.

`release/v0.2.1` still exists and has no MDF4; whether it merges back or is
retired once MDF4 is publishable is open.

The bundled screenshots (`docs/screenshot_*.png`) predate the per-tab filter and
sample links; regenerate them when the next release is cut.

## Deferred

Tracked in [docs/backlog.md](docs/backlog.md): the adapter-repetition call
(BL-E2) remains parked because the loaders have distinct boundaries. Memory-
grid view-richness and new-format backends are in [roadmap.md](roadmap.md).
