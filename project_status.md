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

- **Detail-presenter split committed, build-verify pending** — the large
  `a2ldocumentsession.cpp` had its detail-rendering logic extracted into
  `a2ldetailpresenter.{h,cpp}` + `a2ldetailpresenter_ifdata.cpp`. Committed but
  not yet compiled locally; static review clean. (`docs/backlog.md` BL-E5 and
  the CLAUDE.md "~2300 LOC queued" note describe the pre-split state and need
  reconciling.)
- **Release-artifact fetch blocked (BL-W13)** — configure fetches renamed
  `*parser-*` release assets that do not exist until each parser is re-tagged.

## Deferred

Tracked in [docs/backlog.md](docs/backlog.md): ctest/QTest scaffolding (BL-E1),
engine/controller teardown ordering (BL-E2), trimming aspirational sections out
of `docs/ref/memory_view.md` (BL-E4), and the signal-grid stripe-rendering
documentation (BL-E6). Memory-grid view-richness and new-format backends are in
[roadmap.md](roadmap.md).
