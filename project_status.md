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
- CI (Windows MinGW + Ubuntu) + `release.yml` (Windows zip + Linux AppImage).

## In flight

The MDF4 reader, writer verification gate, explorer backend, and single-channel
plot are built. The remaining work in the locked plan is release integration:
publish the parser artifact on the operator's cadence, pin its integrity hash,
package the backend DLL, and refresh release-facing documentation.

The published `v0.2.0` release consumes the live a2l/dbc/ldf parser releases,
and standalone fetch plus consumer CI are green. The bundled screenshots
(`docs/screenshot_*.png`) still predate the per-tab filter and sample links;
regenerate them when the next release is cut.

## Deferred

Tracked in [docs/backlog.md](docs/backlog.md): the adapter-repetition call
(BL-E2) remains parked because the loaders have distinct boundaries. Memory-
grid view-richness and new-format backends are in [roadmap.md](roadmap.md).
