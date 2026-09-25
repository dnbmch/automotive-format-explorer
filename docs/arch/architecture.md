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
    FormatList            — owned format entries: id, suffixes, adapter
    pending load          — at most one, joined on shutdown
    TabModel              — open documents
    DocumentSession[]     — one per open file, each with its NodeRegistry
    TreeFilterModel[]     — per-session filter proxy over the session's TreeModel
  QQmlApplicationEngine
    Main.qml
      NavPanel          — tree (TreeFilterModel over TreeModel)
      Loader            — MemoryView.qml (A2L) | SignalMapView.qml (DBC/LDF)
                          | SignalPlotView.qml (MDF4) | empty
      Detail            — DetailModel (sections, fields, references)
```

`src/main.cpp` constructs `AppController` from `builtInFormats()` before the QML engine and registers it as a singleton (`qmlRegisterSingletonInstance`). `QCoreApplication::aboutToQuit` calls `AppController::shutdown()`; the QML engine is destroyed next, then the controller.

## Startup: Splash + DWM Cloak

On Windows the standard Qt show-window sequence flashes a white frame while the scene graph initialises. The `QQmlApplicationEngine::objectCreated` handler in `src/main.cpp` works around this with three steps:

1. Set `DWMWA_CLOAK = TRUE` on the window's `HWND` before showing it.
2. Call `window->show()` — the scene graph renders to the framebuffer while the window remains invisible to the compositor.
3. Connect a single-shot handler to `QQuickWindow::frameSwapped` that flips `DWMWA_CLOAK = FALSE`, revealing the window after the first frame is in the framebuffer.

The QML side complements this with [SplashOverlay.qml](../../qml/components/SplashOverlay.qml), which renders a static splash image at full opacity until the first document is ready and then fades out. The combination gives a clean cold-start: no white flash on Windows, no empty-window stretch on slow file loads on either platform.

Non-Windows builds skip the DWMWA dance and call `window->show()` directly.

## Format composition

The application composes its formats once, at the composition boundary:
`builtInFormats()` (`src/builtinformats.cpp`, target `explorer-formats`) returns a
`FormatList` — one `FormatEntry { FormatId id; QStringList extensions;
std::unique_ptr<FormatAdapter> adapter; }` per format, in dialog order: A2L `a2l`,
DBC `dbc`, LDF `ldf`, MDF4 `mf4`. It is the only code that names a concrete adapter.
`main.cpp` moves the list into `AppController`'s constructor, which owns it for its
whole lifetime. Tests compose the same constructor with the production list or with
a fake adapter; there is no second registration path.

Everything format-specific the shell needs derives from that list
(`src/core/formatlist.h`):

- `formatForPath()` matches a path's suffix case-insensitively. A path no entry
  claims is refused with `Unsupported file type: <name>` in `lastError`.
- `fileDialogFilters()` builds the Open dialog's filters — every supported suffix,
  then one filter per format labelled with `formatDisplayName(id)`, then all files —
  exposed to QML as `AppController.fileDialogFilters`.
- `supportedFiles()` lists the supported files of a directory by name.
  `AppController::sampleFiles()` searches `samples/`, `../samples/` and
  `../share/automotive-format-explorer/samples/` next to the executable and offers
  the first non-empty result.

`explorer-core` and the four `explorer-<fmt>-backend` targets are static libraries
on every platform, linked into the one executable; each backend links its parser.
`explorer-core` contains no concrete adapter and no parser. Format identity shown on
a tab is the session's own (`DocumentSession::formatName()`).

### FormatAdapter contract

A backend's adapter has one job: `LoadResult load(const QString& path) const`
returns an owning `DocumentSession` plus diagnostics (`session` is null on hard
failure). Its format identity and suffixes live in the application's `FormatEntry`.

## Opening files and shutdown

`AppController` runs one open at a time: `openFile()` resolves the entry, then runs
`adapter->load()` through `QtConcurrent::run` and watches it with a
`QFutureWatcher<LoadResult>`. The worker moves the new session's models to the
controller's thread before publishing the result; the watcher's `finished`
delivery adds the tab on that thread. A second open while one is pending is refused
with `Another file is already loading.`

Notifications (`lastErrorChanged`, `fileLoadingChanged`, the tab model's row
signals, `currentSessionChanged`, `fileLoaded`) call observers synchronously, and
an observer may open a file or shut the controller down from inside one.
`fileLoading` is therefore true exactly while the watcher holds a load the
controller owns and has not yet consumed: `openFile()` installs the future before
announcing busy, and completion takes its result before announcing idle, so an
open from the idle notification starts the next load and both results are
delivered once each. The controller checks shutdown after clearing errors,
announcing idle and inserting a tab, between the current-tab helper's
`currentTabIndexChanged` and `currentSessionChanged`, and after that helper
returns.

`AppController::shutdown()` is the one teardown path, called from `aboutToQuit`, from
the destructor or from an observer; a repeated call does nothing. It stops accepting
opens, disconnects the watcher's delivery, then waits on the controller's own future
for a load it still owns and takes its result on the controller's thread, where the
session and its models are destroyed — whether the worker was still parsing, had
finished with its completion still queued, or had failed. A result already taken by
an interrupted completion is destroyed there instead, on the same thread. Shutdown
emits nothing; afterwards `fileLoading` is false. The worker never needs the controller's
event loop, and a parse is not interruptible, so shutdown waits for the current
parse to return: this is a lifetime guarantee, not a latency bound. On destruction
the adapter list and every other member are destroyed only afterwards. Once shutdown
has started, no completion adds a tab, changes the current tab, emits `fileLoaded`
or reports an error, and `openFile()` does nothing.

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
  builtinformats  application format list (the only concrete-adapter construction)
  core/         appcontroller, formatlist, noderegistry, detailpresenter,
                detailsection, treeitem, formatid, diagnostics
  models/       treemodel, detailmodel, tabmodel, memorymapmodel, signalmapmodel,
                plotseries, signalplotmodel
  sessions/     documentsession (interface), adaptersessionbase, presentertext
                (shared text/detail helpers), a2l/dbc/ldf/mdf4 sessions and
                detail presenters (a2l splits ifdata helpers into
                a2ldetailpresenter_ifdata.cpp)
  adapters/     formatadapter (load interface) and the a2l/dbc/ldf/mdf4 adapters
  ui/           memorygriditem, signalgriditem, signalplotitem (painted renderers)
qml/
  Main.qml      root layout with SplitView, tabs, Loader
  components/   NavPanel, MemoryView, SignalMapView, SignalPlotView,
                SplashOverlay, Theme, Toast, DiagnosticsPopup
cmake/          DeployRuntimeDeps
```

Each format's detail rendering lives in its own `DetailPresenter` subclass — `a2ldetailpresenter.{h,cpp}`, `dbcdetailpresenter.{h,cpp}`, `ldfdetailpresenter.{h,cpp}`, `mdf4detailpresenter.{h,cpp}` — kept separate from the session files so no session carries both construction/query and the bulk of the detail-building helpers. A2L additionally splits its IF_DATA helpers into `a2ldetailpresenter_ifdata.cpp`. Cross-format text and detail helpers (`text`, `boolText`, `hexId`/`hexValue`, `addField`, `pushSection`, `joinStrings`, `messageToJsonText`) live in `sessions/presentertext.h`, shared by every presenter and document session; `text` decodes protobuf bytes as strict UTF-8 (via `QStringDecoder`, stateless) and falls back to Latin-1 only on a genuine decode error. Format-specific number formatting stays in the per-format headers.

## Memory Ownership

- `AppController` owns its `FormatList` (and through it every adapter), the pending load, `TabModel` with its `DocumentSession`s (each as `std::unique_ptr`), and the per-session filter proxies. A pending load is joined before any of them is destroyed.
- Each `DocumentSession` owns its tree model, detail presenter, and center-panel model. The session outlives every view that binds to those models; QML holds non-owning references via `QAbstractItemModel*`.
- Closing a tab destroys the session, which destroys its `NodeRegistry`, which invalidates every node-key from that document. The UI binds to the new current session and rebuilds detail / center models from scratch.

## See Also

- [../ref/memory_view.md](../ref/memory_view.md) — memory grid visual + interaction spec
- [../ref/signal_map.md](../ref/signal_map.md) — signal grid visual + interaction spec
- [../ref/signal_plot.md](../ref/signal_plot.md) — time-series plot data seam and interaction spec
- [../backlog.md](../backlog.md) — known issues / planned changes
