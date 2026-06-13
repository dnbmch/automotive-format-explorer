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

- **Release-artifact fetch resolved, build-verify pending (BL-W13)** — the
  `CMakeLists.txt` pins point at the published `v0.3.0` / `v0.3.0` / `v0.4.0`
  `-lib` releases that carry the renamed `parser-*` assets, so configure fetches
  existing artifacts. Not yet compiled locally with the new pins.

## Deferred

Tracked in [docs/backlog.md](docs/backlog.md): ctest/QTest scaffolding (BL-E1),
engine/controller teardown ordering (BL-E2), trimming aspirational sections out
of `docs/ref/memory_view.md` (BL-E4), and the signal-grid stripe-rendering
documentation (BL-E6). Memory-grid view-richness and new-format backends are in
[roadmap.md](roadmap.md).
