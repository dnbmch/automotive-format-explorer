# automotive-format-explorer — project status

Current state of play. Direction lives in [roadmap.md](roadmap.md); concrete
deferred items in [docs/backlog.md](docs/backlog.md).

## Built

- Qt 6 / QML desktop app: tree view + detail panel + format-specific center view.
- Plugin-per-format backends (shared `.dll` on Windows via `QLibrary`, static on
  Linux) for A2L, DBC, LDF, each providing `FormatAdapter` / `DocumentSession` /
  `DetailPresenter`.
- A2L memory-map view and DBC/LDF signal-map view via `QQuickPaintedItem`
  C++ renderers (`MemoryGridItem`, `SignalGridItem`) with FBO scrolling.
- Memory grid: overlap hatching (bytes claimed by more than one object),
  click-drag byte-range selection with status readout, and hover tooltips with
  record layout / conversion — see [docs/ref/memory_view.md](docs/ref/memory_view.md).
- Bidirectional selection (tree ↔ detail ↔ center) keyed by `NodeRegistry`.
- Per-tab tree filter (`TreeFilterModel` proxy, `Ctrl+F`) with recursive matching
  and pre-filter state restore.
- Bundled samples (one per format, `samples/`) with "open a sample" links in the
  empty sidebar; provenance in `samples/SAMPLES.md`.
- Splash overlay + DWM cloak startup.
- Links the three parser libraries — fetched from GitHub releases at configure
  time, or staged from the sibling working trees by `seed-parser-deps.sh`. GPL-3.0.
- QTest targets (`tst_treefiltermodel`, `tst_memorymapmodel`,
  `tst_a2ldetailpresenter`) registered with ctest and run in CI.
- CI (Windows MinGW + Ubuntu) + `release.yml` (Windows zip + Linux AppImage).

## In flight

The locked MDF4 plan has completed its independent parser core and phase-3
writer verification gate, including both asammdf producer directions. Phase 4
is next: seed the sibling parser and add the explorer backend, adapter, session,
detail presentation, and dispatch before the phase-5 plot module.

The published `v0.2.0` release consumes the live a2l/dbc/ldf parser releases,
and standalone fetch plus consumer CI are green. The bundled screenshots
(`docs/screenshot_*.png`) still predate the per-tab filter and sample links;
regenerate them when the next release is cut.

## Deferred

Tracked in [docs/backlog.md](docs/backlog.md): the adapter-triplication call
(BL-E2) is parked pending a fourth format. Memory-grid view-richness and
new-format backends are in [roadmap.md](roadmap.md).
