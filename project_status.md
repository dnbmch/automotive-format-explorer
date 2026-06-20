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
- Splash overlay + DWM cloak startup; tab warning indicator.
- Statically links the three parser libraries (fetched from GitHub releases at
  configure time). GPL-3.0.
- CI (Windows MinGW + Ubuntu) + `release.yml` (Windows zip + Linux AppImage).

## In flight

None. The `CMakeLists.txt` parser pins (`v0.3.0` / `v0.3.0` / `v0.4.0`) fetch the
renamed `-lib` assets and a clean configure + build is green (`BL-W13` resolved).

## Deferred

Tracked in [docs/backlog.md](docs/backlog.md): ctest/QTest scaffolding (BL-E1) is
the only open item. Memory-grid view-richness and new-format backends are in
[roadmap.md](roadmap.md).
