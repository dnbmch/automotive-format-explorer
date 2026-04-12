# automotive-format-explorer

Qt/QML desktop app for inspecting A2L, DBC, and LDF automotive files. GPL-3.0.

## Architecture

```
                    ┌─────────────────────────────────┐
                    │       Main.qml (layout)         │
                    │  NavPanel │ CenterPanel │ Detail │
                    └─────┬─────────┬──────────┬──────┘
                          │         │          │
          ┌───────────────┘         │          └──────────────┐
          ▼                         ▼                         ▼
   TreeModel            Loader (per-session)           DetailModel
   (QAbstractItemModel)    MemoryView.qml (A2L)       (QAbstractListModel)
                           SignalMapView.qml (DBC/LDF)
                                    │
                    ┌───────────────┤
                    ▼               ▼
            MemoryGridItem   SignalGridItem
            (QQuickPaintedItem, C++ rendering)
```

### Plugin Architecture

Format backends are shared libraries on Windows (loaded via `QLibrary` at runtime) and static on Linux (linked at build, registered in constructor). Each backend provides:

- `FormatAdapter` — loads a file, returns a `DocumentSession`
- `DocumentSession` — owns the protobuf document, tree model, detail presenter, and optional center panel model
- `DetailPresenter` — builds `QList<DetailSection>` from a `NodeBinding`

### Center Panel Slot

Each `DocumentSession` exposes:
- `centerPanelSource()` — QML component URL (empty = no center panel)
- `centerPanelModel()` — data model for the center panel

Main.qml uses a `Loader` that loads the component and passes the model. When no center panel is available, the layout falls back to two columns.

### Bidirectional Selection

- Tree → Detail: `AppController::selectCurrentNode(nodeKey)` → `DetailPresenter::buildDetails()`
- Tree → Center: `scrollToNodeKey(nodeKey)` on the loaded center panel component
- Center → Tree: `nodeKeyClicked` signal → `AppController::selectCurrentNode()` + `NavPanel::selectAndScrollTo()`

Node keys are assigned by `NodeRegistry` during tree construction. The memory/signal map models store the same keys for cross-referencing.

### Rendering

Both `MemoryGridItem` and `SignalGridItem` extend `QQuickPaintedItem`:
- Pre-computed flat arrays (colorMap, objectMap) for O(1) per-byte/per-bit lookup
- Paint only visible region (viewport-sized item, scroll offset in C++)
- Mouse hover, wheel, click handled in C++ — no QML MouseArea overlay
- `FBO` render target for best scroll performance

## Project Structure

```
src/
  core/           appcontroller, noderegistry, formatid, detailsection, detailpresenter
  models/         treemodel, detailmodel, tabmodel, memorymapmodel, signalmapmodel
  sessions/       documentsession (interface), adaptersessionbase, a2l/dbc/ldf sessions
  adapters/       a2l/dbc/ldf adapter + factory (extern "C" plugin entry points)
  ui/             memorygriditem, signalgriditem (QQuickPaintedItem renderers)
qml/
  Main.qml        root layout with SplitView, tabs, Loader
  components/     NavPanel, MemoryView, SignalMapView, Theme, Toast, SplashOverlay
docs/             design docs, screenshots
cmake/            FetchParserLib, DeployMsys2Deps
```

## Build

```bash
cmake -B build -G Ninja
cmake --build build
```

Qt 6.5+, CMake 3.21+, Protobuf required. Parser libraries are fetched automatically from GitHub releases at configure time.

## Code Conventions

- C++17, Qt6, QML
- PascalCase types, camelCase methods, `snake_case_` private members
- Max ~1256 LOC per file (a2ldocumentsession.cpp is over — needs splitting)
- RAII with unique_ptr, raw pointers for non-owning access
- No exceptions in main flow

## Commits on dnbmch org

- Never add Co-Authored-By lines
- Keep commit messages concise and descriptive

## CI / Release

- `ci.yml` runs on push to master: Windows MinGW + Ubuntu 24.04
- `release.yml` triggers on `v*` tags: builds Windows zip + Linux AppImage, publishes to GitHub Releases
- Do NOT re-tag unless the workflow is verified. Each release build takes ~3 min.

## Platform Differences

| | Windows | Linux |
|---|---------|-------|
| Backends | SHARED (.dll), loaded via QLibrary | STATIC, linked into exe, registered at startup |
| Qt deploy | Manual DLL copy from MSYS2 | AppImage via linuxdeploy |
| Protobuf JSON | `google/protobuf/util/json_util.h` (stable API, works on both v3 and v4+) |
| Define | — | `BACKENDS_STATIC` |
