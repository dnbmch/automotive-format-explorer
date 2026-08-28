# Explorer Architecture

## Purpose

`automotive-format-explorer` is a Qt/QML desktop app for inspecting parsed A2L,
DBC, LDF, and MDF4 files in a tree + detail + center-panel layout. It is the
showcase / front-door product that demonstrates the four parser libraries
integrated in a single binary.

## Process Layout

```
QGuiApplication
  AppController (C++, QML singleton)
    FormatRegistry        — backend lookup by format id
    NodeRegistry          — node-key allocation and reverse lookup
    TabModel              — open documents
    DocumentSession[]     — one per open file
    TreeFilterModel[]     — per-session filter proxy over the session's TreeModel
  QQmlApplicationEngine
    Main.qml
      NavPanel          — tree (TreeFilterModel over TreeModel)
      Loader            — MemoryView.qml (A2L) | SignalMapView.qml (DBC/LDF)
                          | SignalPlotView.qml (MDF4) | empty
      Detail            — DetailModel (sections, fields, references)
```

`AppController` is constructed before the QML engine in `src/main.cpp` and registered as a singleton (`qmlRegisterSingletonInstance`). The QML engine is destroyed first on app exit.

## Startup: Splash + DWM Cloak

On Windows the standard Qt show-window sequence flashes a white frame while the scene graph initialises. The `QQmlApplicationEngine::objectCreated` handler in `src/main.cpp` works around this with three steps:

1. Set `DWMWA_CLOAK = TRUE` on the window's `HWND` before showing it.
2. Call `window->show()` — the scene graph renders to the framebuffer while the window remains invisible to the compositor.
3. Connect a single-shot handler to `QQuickWindow::frameSwapped` that flips `DWMWA_CLOAK = FALSE`, revealing the window after the first frame is in the framebuffer.

The QML side complements this with [SplashOverlay.qml](../../qml/components/SplashOverlay.qml), which renders a static splash image at full opacity until the first document is ready and then fades out. The combination gives a clean cold-start: no white flash on Windows, no empty-window stretch on slow file loads on either platform.

Non-Windows builds skip the DWMWA dance and call `window->show()` directly.

## Plugin / Backend Loading

Each parser (a2l, dbc, ldf, mdf4) is exposed to the explorer as a "backend" via a small C-ABI factory:

- **Windows** — backends are shared libraries (`.dll`) loaded at runtime via `QLibrary`. Each DLL exports `extern "C"` entry points that the explorer resolves to construct the backend's `FormatAdapter`. Each backend DLL (`explorer-<fmt>-backend.dll`) is deployed next to the executable and loaded by exact name from `QCoreApplication::applicationDirPath()` in `AppController::loadBackend()` — there is no `plugins/<format>/` subdirectory.
- **Linux** — backends are static libraries linked into the executable. With `BACKENDS_STATIC` defined, `AppController` explicitly calls and registers the four `create<Fmt>AdapterPlugin()` factories at startup.

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
| `hasDiagnostics()` | `bool` — true when a load produced any diagnostic (warning or error); drives the tab badge |
| `selectNode(quint64 key)` | selects the entity with the given node key |
| `centerPanelSource()` | `QUrl` — QML component URL; empty means the layout collapses to two columns |
| `centerPanelModel()` | `QAbstractListModel*` for the center panel; null when there is no center panel |
| `moveModelsToThread(QThread*)` | moves the session's models to the given thread |

`AdapterSessionBase` (`src/sessions/adaptersessionbase.h`) provides the common machinery (NodeRegistry hookup, tree construction skeleton, diagnostics collection). The per-format sessions (`A2lDocumentSession`, `DbcDocumentSession`, `LdfDocumentSession`, `Mdf4DocumentSession`) inherit from it and supply format-specific tree building, detail sections, and center-panel choice. MDF4 open is metadata-only; its session requests an explicit sample range on a worker only when a plottable channel is selected, caches every completed decode under a byte budget, and applies a result to the plot only while its channel is still the selection.

The nav panel never binds a session's `TreeModel` directly: `AppController::currentTreeModel()` returns a per-session `TreeFilterModel` (`src/models/treefiltermodel.h`) — a `QSortFilterProxyModel` with recursive filtering and auto-accepted child rows that also exposes `nodeKeyRole` and a source-mapped `indexForNodeKey()` to QML. One proxy per session keeps the filter text per tab and preserves NavPanel's model-identity-keyed expand/selection/scroll state; the proxies live in `AppController` and are dropped when their tab closes. Sessions and backends know nothing about filtering.

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

`MemoryGridItem` (A2L memory map), `SignalGridItem` (DBC/LDF signal layout), and
`SignalPlotItem` (format-neutral time series) extend `QQuickPaintedItem`.

The two grid renderers use these rules:

- Pre-computed flat arrays (`colorMap`, `objectMap`) for O(1) per-byte / per-bit lookup.
- Paint only the visible region — the QQuickPaintedItem is sized to the viewport; scroll offsets are tracked in C++.
- Mouse hover, wheel, and click are handled in `mouseMoveEvent` / `wheelEvent` / `mousePressEvent` — no QML `MouseArea` overlay.
- `FBO` render target for stable scroll performance.

The grid items emit `hoveredTooltip` (string) and `nodeKeyClicked(int)` signals; the QML layer is responsible only for placement and signal routing.

The signal plot consumes only `PlotSeries` (`QString` signal/domain metadata plus
parallel `std::vector<double>` domain/value arrays). `SignalPlotModel` builds fixed-size
min/max summaries when a series arrives and derives viewport-width buckets from
those summaries. The paint representation therefore follows the viewport rather than the
recording size. `SignalPlotItem` draws direct polylines when the visible data is
sparse and min/max columns when it is dense; wheel zoom, drag pan, and nearest-
sample cursor lookup remain in the format-neutral plot stack. MDF4 protobuf and
decoder types stop at `Mdf4DocumentSession`.

### Overlap stripes

Both grid items mark cells claimed by more than one occupant. After filling a cell with its color, if the model's overlap query is true (`SignalMapModel::isOverlap(bit)` per bit, `MemoryMapModel::isOverlap(address)` per byte) the item draws diagonal red hatching (`rgba(255,60,60,180)`, 1px pen) clipped to the cell — parallel lines stepped every 6px — over the base fill, so an overlapped cell reads as "colored, with red diagonal lines". The stripe is drawn *before* the selection border and highlight-flash overlay, so those keep visual priority. Overlap detection lives in the models: each rebuilds a per-cell overlap map alongside its occupancy map.

```
+-----+-----+-----+
| sig | sig⟍| sig |   ⟍ = red diagonal hatch on an overlapped cell
+-----+-----+-----+
```

## Project Structure

```
src/
  core/         appcontroller, formatregistry, noderegistry, detailpresenter,
                detailsection, treeitem, formatid, diagnostics
  models/       treemodel, detailmodel, tabmodel, memorymapmodel, signalmapmodel,
                plotseries, signalplotmodel
  sessions/     documentsession (interface), adaptersessionbase, presentertext
                (shared text/detail helpers), a2l/dbc/ldf/mdf4 sessions and
                detail presenters (a2l splits ifdata helpers into
                a2ldetailpresenter_ifdata.cpp)
  adapters/     a2l/dbc/ldf/mdf4 adapter + factory (C plugin entry points)
  ui/           memorygriditem, signalgriditem, signalplotitem (painted renderers)
qml/
  Main.qml      root layout with SplitView, tabs, Loader
  components/   NavPanel, MemoryView, SignalMapView, SignalPlotView,
                SplashOverlay, Theme, Toast, DiagnosticsPopup
cmake/          FetchParserLib, DeployRuntimeDeps
```

Each format's detail rendering lives in its own `DetailPresenter` subclass — `a2ldetailpresenter.{h,cpp}`, `dbcdetailpresenter.{h,cpp}`, `ldfdetailpresenter.{h,cpp}`, `mdf4detailpresenter.{h,cpp}` — kept separate from the session files so no session carries both construction/query and the bulk of the detail-building helpers. A2L additionally splits its IF_DATA helpers into `a2ldetailpresenter_ifdata.cpp`. Cross-format text and detail helpers (`text`, `boolText`, `hexId`/`hexValue`, `addField`, `pushSection`, `joinStrings`, `messageToJsonText`) live in `sessions/presentertext.h`, shared by every presenter and document session; `text` decodes protobuf bytes as strict UTF-8 (via `QStringDecoder`, stateless) and falls back to Latin-1 only on a genuine decode error. Format-specific number formatting stays in the per-format headers.

## Memory Ownership

- `AppController` owns `FormatRegistry`, `NodeRegistry`, `TabModel`, and the list of `DocumentSession`s (each as `std::unique_ptr`).
- Each `DocumentSession` owns its tree model, detail presenter, and center-panel model. The session outlives every view that binds to those models; QML holds non-owning references via `QAbstractItemModel*`.
- Closing a tab destroys the session, which destroys its `NodeRegistry`, which invalidates every node-key from that document. The UI binds to the new current session and rebuilds detail / center models from scratch.

## See Also

- [../ref/memory_view.md](../ref/memory_view.md) — memory grid visual + interaction spec
- [../ref/signal_map.md](../ref/signal_map.md) — signal grid visual + interaction spec
- [../ref/signal_plot.md](../ref/signal_plot.md) — time-series plot data seam and interaction spec
- [../backlog.md](../backlog.md) — known issues / planned changes
