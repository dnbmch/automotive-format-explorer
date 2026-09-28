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
    TabModel              — open documents, one DocumentTab each
      DocumentTab         — owns its session and the filter over the session's tree
        DocumentSession   — binds its rows to its own entities
        TreeFilterModel   — the tab's filter proxy over the session's TreeModel
  QQmlApplicationEngine
    Main.qml
      NavPanel          — the current tab's tree (TreeFilterModel over TreeModel)
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
announcing idle and inserting a tab, between `currentTabIndexChanged` and
`currentSessionChanged`, and after announcing the new current tab. The completion
selects the tab it inserted by identity, never by its row: see [Tabs](#tabs).

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
or reports an error, and every public action does nothing: `openFile()`,
`closeTab()`, `setCurrentTabIndex()`, `selectCurrentNode()`, `clearLastError()`,
`setStartupLoading()` and `setStartupStatusText()`. A tab model row change already
under way when an observer shuts down still completes; nothing follows it. The
remaining tabs are destroyed with the controller.

## Tabs

Each open file is one `DocumentTab` (`src/core/documenttab.h`), owned by the
`TabModel`. The tab owns its session and the `TreeFilterModel`
(`src/models/treefiltermodel.h`) over the session's tree, and is the proxy's only
writer (`setFilterText()`). The proxy filters recursively, auto-accepts the children
of a match and exposes `nodeKeyRole` and a source-mapped `indexForNodeKey()` to QML;
the nav panel shows it and never binds a session's `TreeModel` directly. Sessions and
backends know nothing about filtering. QML reaches the current tab through
`AppController.currentTab`, a property; a tab is never the result of an invokable,
which would hand it to the JavaScript engine.

The controller tracks the current tab by identity, and `currentTabIndex` is that tab's
row, or -1 for none. `currentTabIndexChanged` announces a new row or a new tab at the
same row, so a view bound to the index (the tab bar, whose own index moves when its
current delegate is removed) re-reads it. `currentSessionChanged` announces a new
current tab and is the notification for `currentTab`, `currentDetailModel` and the
center panel. The controller remembers what it last announced and sends each
notification only when the state differs from it, and never after shutdown; it
re-reads its state after every notification, so a change an observer makes from one
announces itself and nothing stale follows it.

- `setCurrentTabIndex(index)` does nothing after shutdown or for an index outside
  -1…count-1. Otherwise it makes that tab current (-1: none) and announces it.
- `closeTab(index)` does nothing after shutdown or for an index out of range.
  Otherwise, if the tab is current, its successor becomes current, else its
  predecessor, else none; then the tab leaves the tab model, whose row notifications
  already see the new current tab; then the change is announced. The closed tab,
  with its session, filter and models, is destroyed on the controller's thread after
  those notifications return, so no view reaches a destroyed model through the
  controller's properties. Closing a tab before the current one only moves the
  current row: the center view is not rebuilt.
- An observer may switch, close or open tabs, or shut down, from any controller
  notification. Each nested close destroys its own tab once its own notifications
  have returned.
- A tab model row insertion or removal is a Qt model transaction whose notifications
  run synchronously; another row change inside it would break the model. A switch or
  close requested during one is therefore deferred to the event loop. It names its
  tab, not its row, and is revalidated on delivery: it does nothing once that tab has
  left the model or the controller has shut down. A request to make no tab current is
  kept as such.
- A load completion inserts its tab and makes that tab current by identity; a close
  of it requested during the insertion follows from the event loop.
- Reentry from an observer is direct and synchronous. An observer of a tab model row
  notification must not run a nested event loop (a modal dialog,
  `QEventLoop::exec()`): a load completing inside it would insert its tab within the
  row transaction; deferred requests delivered there only defer again. Work that
  needs an event loop is scheduled to run after the transaction.

### Tree navigation

A tab also keeps the tree navigation the nav panel last saved for it: the keys of
the expanded rows in row order, so parents precede children, the current row's key
and the scroll position. Every row, categories included, has a key, so every saved
row is found again. `NavPanel.qml` saves the view into the tab it leaves and applies
the navigation of the tab it shows once the new rows are laid out; it saves only the
tab whose navigation it has applied, never a newly bound view awaiting its restore.
Applying makes the saved row current by its model index, even when a collapsed parent
hides it, and a saved key 0 leaves no row current; expansion and scroll position are
applied as saved. Restoring moves only the tree's current row: it selects nothing in
the detail or center panel, which keep their session's last selection. Each scheduled
restore carries a generation. A tab or filter change starts a new
one, so an older restore, or one whose tab was destroyed meanwhile, does nothing.
Run late, an older restore would reach the view: `TreeView` matches another model's
index by its row and parent, so another tab's saved expansion would open the shown
tree's top-level rows at the same positions, and over a filtered view it would move
the current row and scroll position.
Saving first brings the view's layout up to date: the row count it walks follows the
layout, which lags an expansion made in the same event-loop turn.

The filter keeps a pre-filter snapshot in the tab. Entering a filter saves the view
as the tab's navigation, which the tab keeps as the snapshot; while filtered, saves
replace only the navigation; clearing the filter makes the snapshot the navigation
again, and the panel applies it. A filter entered while a restore is pending starts
from the tab's saved navigation. Showing a tab sets the filter field's text without
starting a filter change.

A tab keeps tree navigation only. Center models keep the view state they own (A2L
segment and bytes per row, DBC/LDF message and multiplexer group, MDF4 plot range);
state a painted item holds resets when the center view is rebuilt for a shown tab
([BL-E5](../backlog.md#bl-e5-center-grid-view-state-is-not-kept-per-tab)).

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
| `moveModelsToThread(QThread*)` | moves the session's `QObject`s — its models, and for MDF4 the scan watcher — to the given thread |

`AdapterSessionBase` (`src/sessions/adaptersessionbase.h`) provides what every format shares: identity, diagnostics, the tree and detail models, and row keys. The per-format sessions (`A2lDocumentSession`, `DbcDocumentSession`, `LdfDocumentSession`, `Mdf4DocumentSession`) inherit from it; each builds its tree, binds its entity rows to its own typed entity paths, answers `selectNode()` through its own presenter, and chooses its center panel. MDF4 open indexes metadata only; channel samples are scanned on demand from the same opened file, as described in [MDF4 reads](#mdf4-reads).

## MDF4 reads

`Mdf4Adapter::load()` opens one `mdf4::Reader` per file, with the reader's default
limits and no cancellation: opening runs to its end, like every format's load, so
shutdown during a large MDF4 opening waits for it. The session's tree, detail
presenter, time axes and every scan use that reader: the session holds its metadata
through a `std::shared_ptr<const mdf4::File>` aliasing the reader, and a scan function
and an axis function bound to it. Any of them keeps the reader alive; there is no
second metadata copy and no path-based read. A file that cannot be opened, or whose
opening a reader limit refused, still opens as a session showing its diagnostics (a
refusal is one DROPPED diagnostic), and its scans fail. The reader's own limits,
outcomes, traversal and cancellation checkpoints are the
[reader contract](../../../mdf4-parser/docs/arch/reader.md).

Both session allowances below are per session: every open MDF4 tab adds its own, and
nothing bounds the process as a whole. They implement the per-file budgets of the
[resource envelope](../../../docs/plans/i2_read_envelope.md) (a 16 GB laptop, one
admitted recording up to 64 GB).

**Tree.** Before any group or channel row exists, `Mdf4DocumentSession::treeBytes()`
estimates the tree from the metadata: 352 B per row (its item, its slot in the
parent's children, its key and path table entries) plus 9/4 B per UTF-8 byte of title
and subtitle text. Past `Mdf4SessionLimits::treeBytes` (384 MiB) the file row stands
alone, with its details, and one error diagnostic, "Channel tree not shown", gives the
counts, the estimate and the allowance; no row stands for part of the tree. 384 MiB
covers the tree of any file the reader's 1 GiB opening allowance admits with realistic
channel text: about 873 k channels at 1,230 B of opening charge each, a tree of about
367 MiB.

**Result sets.** Selecting a plottable channel scans its whole metadata range once
into an overview; the overview decides the domain of its result set. The plot then
names the exact window its view wants (`SignalPlotModel::detailRequest()`, announced
by `detailWanted`): a scan of the conservative index cover the overview's bins give.
Both are built on the worker from the scan's chunks
([signal plot](../ref/signal_plot.md)). The domain's name and unit are those of the
time master `Reader::axis()` resolved at opening, local or remote; a group without
one plots against the sample index. The session resolves no master itself.

**Scheduling.** A session runs one scan at a time, so the reader is never used
concurrently. It keeps the scan in flight (channel, overview or window with its range
and result set, cancellation flag and reserved bytes) and at most one pending scan,
the latest wanted:

- Selecting a channel with a cached overview shows it at once, and the plot's detail
  request follows. Otherwise an overview in flight for that channel is kept; else the
  overview becomes the pending scan and the scan in flight is cancelled.
- A new view is served by a cached window that covers it, or by a window in flight
  whose range covers it; otherwise the window the plot names becomes the pending scan
  and a window in flight is cancelled. A view with more samples than one window holds
  keeps the overview and cancels windows.
- Selecting a master, an unsupported channel or a row that is no channel drops the
  pending scan and cancels the one in flight.
- The completion of a cancelled scan is taken before the pending scan starts, even
  when the scan finished before it saw the cancellation. Its result is released
  while its reservation still stands, then its request with the reservation, before
  anything else is admitted or announced.

The worker passes on progress at most once per 100 ms and only when it grew; the plot
shows the progress of the scan in flight only while that scan is not cancelled and
belongs to the selection.

The task captures a copy of the scan function and plain request values, never the
session, a model or a result. It hands over its result only after the scan returned
Ok, which the reader reports after checking the source once more, so no chunk of a
cancelled, refused or failed scan reaches the plot. Each launch creates one
cancellation flag, shared by the scan in flight and its task, which passes it to the
reader as `Control::cancel`; cancelling sets that flag and nothing else, and a
completion whose flag is set is obsolete however far its scan got. One session-owned
`QFutureWatcher` watches the task in flight. A scan launches only when none is in
flight, so the watcher is handed a new task only after the previous completion was
taken. It delivers completions and progress, and it moves with the models when the
open worker hands the session to the controller thread. Plot notifications reach
observers synchronously, and an observer may select another node, change the view or
close the session from them. Each flow therefore settles the scans, the cache and the
selection before notifying, notifies last, the busy state last of all, and touches
nothing after a notification that destroyed the session. The busy state is read
again after the progress notification, whose observer may have started other work.
The plot model survives its own destruction from any of its notifications. The detail
model's notification in `selectNode()` comes before the plot flow and outside this
rule: the flow after it assumes the session and the selection still stand, and no
observer closes or reselects from it
([BL-V3](../backlog.md#bl-v3-an-mdf4-selection-notifies-the-detail-panel-before-its-plot-flow)).

**Storage.** One allowance per session, `Mdf4SessionLimits::resultBytes` (256 MiB),
covers the retained results, cached or shown, and the reservation of the scan in
flight. A scan's `PlotOverviewBuilder::reservation()` or
`PlotWindowBuilder::reservation()` is admitted before it launches; its completion
replaces the reservation with the result's own `bytes()` in the same step, and a
cancelled, refused or failed scan releases it; a cancelled scan's result goes first,
so no result outlives its charge. Admission evicts the least recently used
results nobody else holds. A result is held while the plot shows it, a window pending
or in flight holds the overview of its result set, and a cached window holds that
overview too, so a window goes before its overview. When held results leave no room,
the scan is refused and the plot says how much it needed and how much is held; no
result is exempt from the allowance. An overview is found by its channel; a window by
its channel, its result set (and with it the domain) and whether it covers the view.
256 MiB holds about a thousand 4,096-bin overviews of 256 KiB each; an evicted one is
scanned again when wanted.

**Outcomes.** A scan that returns Ok with samples shows the overview, then the view's
window; Short coverage shows "N of M samples" and bounds the view by the last sample
read. Ok with no samples shows "No samples recorded" (or that the recording ends
before its first sample) and is kept like any result. A cancelled scan shows and keeps
nothing. ResourceLimit shows the refusal with the reader's numbers, or the window's
sample limit; SourceChanged, and a window whose samples contradict their overview, ask
to reopen the file; Error gives the reader's reason and location. Refused and failed
scans are not kept: selecting the channel again scans again.

**Teardown.** Destroying the session disconnects delivery, drops the pending scan,
cancels the one in flight and waits on this thread until it returns; its result is
released there. The task may drop its copy of the scan function on the worker
afterwards; nothing else touches it. The reader checks cancellation at its
checkpoints, so closing a tab waits for the next one, and for a blocking file call in
progress.

## Node keys

Every tree row gets a nonzero session-local key, `nodeKey`, as the session appends it: `AdapterSessionBase::appendNode()` numbers rows 1, 2, … in creation order, categories included, and only the invisible root keeps 0. `TreeModel::setRoot()` records each row's position under its parent and files the row by key once, so `indexForNodeKey()` and `parent()` walk nothing. A format session keeps its own table from the key of each entity row to that format's typed entity path (`A2lPath`, `DbcPath`, `LdfPath` or `Mdf4Path`, declared with the format's presenter); shared code names no format. A category row has a key but no entity. The key is the universal cross-reference token used by:

- The format session, to find the selected row's entity and build its details.
- The center panel models (memory grid, signal grid), to render highlights at the right offset / bit and to emit `nodeKeyClicked` when the user clicks a region.
- `AppController::selectCurrentNode(qulonglong nodeKey)` and `NavPanel.selectAndScrollTo(nodeKey)`, which together implement bidirectional selection.

Keys are scoped per session — a key from one document is never valid in another.

## Bidirectional Selection

```
NavPanel (tree row click)
    └─► AppController::selectCurrentNode(nodeKey)
            └─► the session's presenter builds the row entity's details
            └─► center panel: scrollToNodeKey(nodeKey)

Center panel (memory grid / signal grid click)
    └─► nodeKeyClicked(nodeKey)
            └─► AppController::selectCurrentNode(nodeKey)
                    └─► (same path as above)
            └─► NavPanel::selectAndScrollTo(nodeKey)
```

`AppController` is the single mediator. The tree, detail, and center panels never call each other directly — they all go through the controller and identify entities by `nodeKey`.

Selecting a row builds its cards at once; its raw JSON is produced only when read. The session hands `DetailModel::setSelection()` the cards and a producer of the entity's raw JSON, or none when the entity has no raw form (LDF's overview). `rawJsonAvailable` answers from the producer without serializing; the raw toggle is one preference of the detail panel, not per tab. The first read of `rawJsonText` after a selection runs the producer synchronously on the GUI thread and keeps its text, an empty one included, until the next selection; the raw view reads it only while shown. An aggregate row (an A2L module, the MDF4 file) still serializes its whole entity when the raw view is opened on it.

## Rendering

`MemoryGridItem` (A2L memory map), `SignalGridItem` (DBC/LDF signal layout), and
`SignalPlotItem` (format-neutral time series) extend `QQuickPaintedItem`.

The two grid renderers use these rules:

- `SignalGridItem` precomputes per-bit color arrays; a frame spans at most a few hundred bits.
- `MemoryGridItem` holds no per-byte state. It paints the visible rows from one
  `MemoryMapModel::queryBytes` tile, which resolves ownership and overlap from the
  segment's sorted object intervals, and hit-tests through the same query; object
  colors are assigned once per segment. Row geometry is 64-bit, so a segment of any
  size scrolls, paints and selects correctly.
- Paint only the visible region — the QQuickPaintedItem is sized to the viewport; scroll offsets are tracked in C++.
- Mouse hover, wheel, and click are handled in `mouseMoveEvent` / `wheelEvent` / `mousePressEvent` — no QML `MouseArea` overlay.
- `FBO` render target for stable scroll performance.

The grid items expose a `hoveredTooltip` string and emit `nodeKeyClicked(qulonglong)`; the QML layer is responsible only for placement and signal routing.

Each painted item holds its model through a `QPointer`. A replaced center view lives until a later event-loop turn, so it can outlive its model; the model's destruction then counts as being given no model. The item drops the hover, selections, highlight flash and drag that name the model's rows, notifies `modelChanged`, paints its empty state and ignores input.

The signal plot consumes only the format-neutral results of `src/models/plotdata.h`:
an overview of at most 4,096 bins and exact windows of at most 4 Mi samples, which a
producer's worker builds from ordered sample chunks, with their summaries, domain
decision and zoom floor. `SignalPlotModel` installs them immutable and does only
viewport-sized work on the GUI thread: one extrema column per pixel from the bins or
from a window's block summaries. `SignalPlotItem` draws overview bins as columns
never joined to each other, an exact window as a line while it is sparser than two
samples per pixel and as columns beyond, and handles wheel zoom, drag pan and hover.
MDF4 protobuf and reader types stop at `Mdf4DocumentSession`. Details:
[signal plot](../ref/signal_plot.md).

### Overlap stripes

Both grid items mark cells claimed by more than one occupant. After filling a cell with its color, if the model's overlap query is true (`SignalMapModel::isOverlap(bit)` per bit, `MemoryMapModel::isOverlap(address)` per byte) the item draws diagonal red hatching (`rgba(255,60,60,180)`, 1px pen) clipped to the cell — parallel lines stepped every 6px — over the base fill, so an overlapped cell reads as "colored, with red diagonal lines". The stripe is drawn *before* the selection border and highlight-flash overlay, so those keep visual priority. Overlap detection lives in the models: the signal map rebuilds a per-bit overlap map alongside its occupancy map, and the memory map answers overlap from its object intervals in the same byte query that decides ownership.

```
+-----+-----+-----+
| sig | sig⟍| sig |   ⟍ = red diagonal hatch on an overlapped cell
+-----+-----+-----+
```

## Project Structure

```
src/
  builtinformats  application format list (the only concrete-adapter construction)
  core/         appcontroller, documenttab, formatlist, detailsection, treeitem,
                formatid, diagnostics
  models/       treemodel, treefiltermodel, detailmodel, tabmodel, memorymapmodel,
                signalmapmodel, plotdata (overview, window and their builders),
                signalplotmodel
  sessions/     documentsession (interface), adaptersessionbase, presentertext
                (shared text/detail helpers), a2l/dbc/ldf/mdf4 sessions and
                detail presenters (a2l splits ifdata helpers into
                a2ldetailpresenter_ifdata.cpp)
  adapters/     formatadapter (load interface) and the a2l/dbc/ldf/mdf4 adapters
  ui/           memorygriditem, signalgriditem, signalplotitem (painted renderers),
                gridpalette (shared palette, shade and highlight-flash helpers)
qml/
  Main.qml      root layout with SplitView, tabs, Loader
  components/   NavPanel, MemoryView, SignalMapView, SignalPlotView,
                SplashOverlay, Theme, Toast, DiagnosticsPopup
cmake/          DeployRuntimeDeps
```

Each format's detail rendering lives in its own presenter class, which also declares that format's entity kinds and path — `a2ldetailpresenter.{h,cpp}`, `dbcdetailpresenter.{h,cpp}`, `ldfdetailpresenter.{h,cpp}`, `mdf4detailpresenter.{h,cpp}` — kept separate from the session files so no session carries both construction/query and the bulk of the detail-building helpers. A2L additionally splits its IF_DATA helpers into `a2ldetailpresenter_ifdata.cpp`. Cross-format text and detail helpers (`text`, `boolText`, `hexId`/`hexValue`, `addField`, `pushSection`, `joinStrings`, `messageToJsonText`) live in `sessions/presentertext.h`, shared by every presenter and document session; `text` decodes protobuf bytes as strict UTF-8 (via `QStringDecoder`, stateless) and falls back to Latin-1 only on a genuine decode error. Format-specific number formatting stays in the per-format headers.

## Memory Ownership

- `AppController` owns its `FormatList` (and through it every adapter), the pending load, and `TabModel` with its `DocumentTab`s; each tab owns its session and its filter proxy, destroyed first. A pending load is joined before any of them is destroyed.
- Each `DocumentSession` owns its tree and detail models, its presenter and its center-panel model. QML holds non-owning references; each painted item tracks its model's lifetime (see [Rendering](#rendering)).
- Plot results are immutable and shared: an MDF4 session's cache and its plot model hold the same overview and window, and the plot holds only what it shows, dropping a window once the view leaves it. The session accounts every result it retains and the reservation of its scan in flight against one allowance, and evicts only results nobody else holds ([MDF4 reads](#mdf4-reads)).
- Closing a tab takes it out of the `TabModel`, announces the new current tab, and then destroys the tab with its session, filter and models, and every key from that document. The UI has already bound the new current tab's models by then.

## See Also

- [../ref/memory_view.md](../ref/memory_view.md) — memory grid visual + interaction spec
- [../ref/signal_map.md](../ref/signal_map.md) — signal grid visual + interaction spec
- [../ref/signal_plot.md](../ref/signal_plot.md) — time-series plot data seam and interaction spec
- [../backlog.md](../backlog.md) — known issues / planned changes
