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
- Bidirectional selection (tree ↔ detail ↔ center) keyed by `NodeRegistry`.
- Per-tab tree filter (`TreeFilterModel` proxy, `Ctrl+F`) with recursive matching
  and pre-filter state restore.
- Bundled samples (one per format, `samples/`) with "open a sample" links in the
  empty sidebar; provenance in `samples/SAMPLES.md`.
- Splash overlay + DWM cloak startup.
- Links the three parser libraries — fetched from GitHub releases at configure
  time, or staged from the sibling working trees by `seed-parser-deps.sh`. GPL-3.0.
- QTest targets (`tst_treefiltermodel`, `tst_a2ldetailpresenter`) registered with
  ctest and run in CI.
- CI (Windows MinGW + Ubuntu) + `release.yml` (Windows zip + Linux AppImage).

## In flight

A standalone clean configure cannot download the parser `-lib` artifacts: the
tags pinned in `CMakeLists.txt` are ahead of what is published. The in-workspace
build (`seed-parser-deps.sh` then `build.sh`) is green with the tests passing;
publishing the pinned parser tags is what unblocks the standalone fetch.

The working tree is ahead of the published `v0.1.0` release. The bundled
screenshots (`docs/screenshot_*.png`) predate the per-tab filter and sample
links; regenerate them when the next release is cut.

## Deferred

Tracked in [docs/backlog.md](docs/backlog.md): the adapter-triplication call
(BL-E2) is parked pending a fourth format. Memory-grid view-richness and
new-format backends are in [roadmap.md](roadmap.md).
