# Explorer Architecture

## Purpose

`automotive-format-explorer` is a Qt/QML desktop app for inspecting parsed A2L, DBC, and LDF files in a tree + detail + center-panel layout. It is the showcase / front-door product that demonstrates the three parser libraries integrated in a single binary.

## Process Layout

```
QGuiApplication
  AppController (C++, QML singleton)
    FormatRegistry        — backend lookup by format id
    NodeRegistry          — node-key allocation and reverse lookup
    TabModel              — open documents
    DocumentSession[]     — one per open file
  QQmlApplicationEngine
    Main.qml
      NavPanel          — tree (TreeModel)
      Loader            — MemoryView.qml (A2L) | SignalMapView.qml (DBC/LDF) | empty
      Detail            — DetailModel (sections, fields, references)
```

`AppController` is constructed before the QML engine in `src/main.cpp` and registered as a singleton (`qmlRegisterSingletonInstance`). The QML engine is destroyed first on app exit.

## Startup: Splash + DWM Cloak

On Windows the standard Qt show-window sequence flashes a white frame while the scene graph initialises. `src/main.cpp:40-58` works around this with three steps tied to the engine's `objectCreated` signal:

1. Set `DWMWA_CLOAK = TRUE` on the window's `HWND` before showing it.
2. Call `window->show()` — the scene graph renders to the framebuffer while the window remains invisible to the compositor.
3. Connect a single-shot handler to `QQuickWindow::frameSwapped` that flips `DWMWA_CLOAK = FALSE`, revealing the window after the first frame is in the framebuffer.

The QML side complements this with [SplashOverlay.qml](../../qml/components/SplashOverlay.qml), which renders a static splash image at full opacity until the first document is ready and then fades out. The combination gives a clean cold-start: no white flash on Windows, no empty-window stretch on slow file loads on either platform.

Non-Windows builds skip the DWMWA dance and call `window->show()` directly.

## Plugin / Backend Loading

Each parser (a2l, dbc, ldf) is exposed to the explorer as a "backend" via a small C-ABI factory:

- **Windows** — backends are shared libraries (`.dll`) loaded at runtime via `QLibrary`. Each DLL exports `extern "C"` entry points that the explorer resolves to construct the backend's `FormatAdapter`. Each backend DLL (`explorer-<fmt>-backend.dll`) is deployed next to the executable and loaded by exact name from `QCoreApplication::applicationDirPath()` (`src/core/appcontroller.cpp:254-255`) — there is no `plugins/<format>/` subdirectory.
- **Linux** — backends are static libraries linked into the executable. Each backend self-registers in a constructor; the `FormatRegistry` collects registrations on startup. Built with `-DBACKENDS_STATIC`.

The `FormatRegistry` (`src/core/formatregistry.h`) is the single lookup point: given a `FormatId`, return the `FormatAdapter*` that can load files of that type. The platform difference is invisible above this layer.

### FormatAdapter contract

A backend exposes:

- `FormatId formatId()` — stable enum id (A2L / DBC / LDF / …)
- `QString formatName()` / `QStringList extensions()` — display name and the file extensions it claims (matched case-insensitively)
- `LoadResult load(const QString& path)` — returns the loaded `DocumentSession` plus diagnostics (`session` is null on hard failure)

## DocumentSession Contract

A `DocumentSession` (interface in `src/sessions/documentsession.h`) is the per-document anchor that the UI binds to. It exposes:

| Method | Returns / does |
|---|---|
| `formatId()` | `FormatId` — stable enum id of the document's format |
| `formatName()` | `QString` — human-readable format name |
| `displayName()` | `QString` — tab label for the document |
| `sourcePath()` | `QString` — path of the loaded source file |
| `treeModel()` | `TreeModel*` for the nav panel |
| `detailModel()` | `DetailModel*` — the detail panel's section/field model |
| `diagnostics()` | `QList<DiagnosticMessage>` — diagnostics gathered during load |
| `hasWarnings()` | `bool` — true when a load produced warning-severity diagnostics (none are emitted yet) |
| `selectNode(quint64 key)` | selects the entity with the given node key |
| `centerPanelSource()` | `QUrl` — QML component URL; empty means the layout collapses to two columns |
| `centerPanelModel()` | `QAbstractListModel*` for the center panel; null when there is no center panel |
| `moveModelsToThread(QThread*)` | moves the session's models to the given thread |

`AdapterSessionBase` (`src/sessions/adaptersessionbase.h`) provides the common machinery (NodeRegistry hookup, tree construction skeleton, diagnostics collection). The per-format sessions (`A2lDocumentSession`, `DbcDocumentSession`, `LdfDocumentSession`) inherit from it and supply format-specific tree building, detail sections, and center-panel choice.

## NodeRegistry and node keys

Every entity rendered in the tree gets a stable integer `nodeKey` allocated by `NodeRegistry` during tree construction. The key is the universal cross-reference token used by:

- The detail presenter, to fetch the right entity when a tree row is selected.
- The center panel models (memory grid, signal grid), to render highlights at the right offset / bit and to emit `nodeKeyClicked` when the user clicks a region.
- `AppController::selectCurrentNode(int nodeKey)` and `NavPanel::selectAndScrollTo(int nodeKey)`, which together implement bidirectional selection.

Keys are scoped per session — a key from one document is never valid in another. The registry survives as long as its owning session.

## Bidirectional Selection

```
NavPanel (tree row click)
    └─► AppController::selectCurrentNode(nodeKey)
            └─► DetailPresenter::buildDetails(nodeKey)
            └─► center panel: scrollToNodeKey(nodeKey)

Center panel (memory grid / signal grid click)
    └─► nodeKeyClicked(nodeKey)
            └─► AppController::selectCurrentNode(nodeKey)
                    └─► (same path as above)
            └─► NavPanel::selectAndScrollTo(nodeKey)
```

`AppController` is the single mediator. The tree, detail, and center panels never call each other directly — they all go through the controller and identify entities by `nodeKey`.

## Rendering

`MemoryGridItem` (A2L memory map) and `SignalGridItem` (DBC/LDF signal layout) both extend `QQuickPaintedItem`:

- Pre-computed flat arrays (`colorMap`, `objectMap`) for O(1) per-byte / per-bit lookup.
- Paint only the visible region — the QQuickPaintedItem is sized to the viewport; scroll offsets are tracked in C++.
- Mouse hover, wheel, and click are handled in `mouseMoveEvent` / `wheelEvent` / `mousePressEvent` — no QML `MouseArea` overlay.
- `FBO` render target for stable scroll performance.

The grid items emit `hoveredTooltip` (string) and `nodeKeyClicked(int)` signals; the QML layer is responsible only for placement and signal routing.

### Overlap stripes

`SignalGridItem` marks bits claimed by more than one signal. After filling a cell with its signal color, if `SignalMapModel::isOverlap(bit)` is true it draws diagonal red hatching (`rgba(255,60,60,180)`, 1px pen) clipped to the cell — parallel lines stepped every 6px — over the base fill, so an overlapped bit reads as "colored, with red diagonal lines". The stripe is drawn *before* the selection border and highlight-flash overlay, so those keep visual priority.

```
+-----+-----+-----+
| sig | sig⟍| sig |   ⟍ = red diagonal hatch on an overlapped bit
+-----+-----+-----+
```

`MemoryGridItem` does not draw overlap stripes: A2L objects sit at distinct addresses, so overlap visualization on the memory grid is a planned enhancement (see [../plans/memory_view_planned.md](../plans/memory_view_planned.md)).

## Project Structure

```
src/
  core/         appcontroller, formatregistry, noderegistry, detailpresenter,
                detailsection, treeitem, formatid, diagnostics
  models/       treemodel, detailmodel, tabmodel, memorymapmodel, signalmapmodel
  sessions/     documentsession (interface), adaptersessionbase, a2l/dbc/ldf
                sessions, a2l detail presenter (+ ifdata helpers)
  adapters/     a2l/dbc/ldf adapter + factory (extern "C" plugin entry points)
  ui/           memorygriditem, signalgriditem (QQuickPaintedItem renderers)
qml/
  Main.qml      root layout with SplitView, tabs, Loader
  components/   NavPanel, MemoryView, SignalMapView, SplashOverlay, Theme, Toast
cmake/          FetchParserLib, DeployMsys2Deps
```

A2L detail rendering lives in `a2ldetailpresenter.{h,cpp}` (+ `a2ldetailpresenter_ifdata.cpp`), a `DetailPresenter` subclass split out of `a2ldocumentsession.cpp` so no session file carries both construction/query and the bulk of the detail-building helpers.

## Memory Ownership

- `AppController` owns `FormatRegistry`, `NodeRegistry`, `TabModel`, and the list of `DocumentSession`s (each as `std::unique_ptr`).
- Each `DocumentSession` owns its tree model, detail presenter, and center-panel model. The session outlives every view that binds to those models; QML holds non-owning references via `QAbstractItemModel*`.
- Closing a tab destroys the session, which destroys its `NodeRegistry`, which invalidates every node-key from that document. The UI binds to the new current session and rebuilds detail / center models from scratch.

## See Also

- [../ref/memory_view.md](../ref/memory_view.md) — memory grid visual + interaction spec
- [../ref/signal_map.md](../ref/signal_map.md) — signal grid visual + interaction spec
- [../backlog.md](../backlog.md) — known issues / planned changes
