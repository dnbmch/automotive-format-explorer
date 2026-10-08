# Adapter contract

How to add another format to the explorer.

Each format ships an adapter (loads a file → returns a session) and a session (owns the parsed document and exposes models to QML). Both compile into a static backend library that is linked into the one executable on every platform; the application's format list constructs the adapter.

## What you need to write

| File | Purpose |
|---|---|
| `src/adapters/<fmt>adapter.h` + `.cpp` | `class <Fmt>Adapter final : public FormatAdapter` implementing `load()` |
| `src/sessions/<fmt>documentsession.h` + `.cpp` | `class <Fmt>DocumentSession : public AdapterSessionBase` (or directly `DocumentSession`) — owns the parsed proto document, builds the `TreeModel`, populates the `DetailModel` per node click, optionally exposes a center-panel model |
| Canonical parser target in `CMakeLists.txt` | Supplies matched headers and static library from source composition or an installed package |
| Entry in `src/builtinformats.cpp` | `{FormatId::<FMT>, {"<ext>"}, std::make_unique<<Fmt>Adapter>()}` — the format's identity, suffixes and adapter in one place |

The four existing implementations under `src/adapters/` and `src/sessions/`
are the working references. DBC is the smallest metadata-at-open template;
MDF4 is the reference for metadata-only open followed by lazy bulk-data work.

## FormatAdapter interface

```cpp
class FormatAdapter {
public:
    virtual ~FormatAdapter() = default;
    virtual LoadResult load(const QString& path, const std::atomic<bool>& cancel) const = 0;
};

struct LoadResult {
    std::unique_ptr<DocumentSession> session;         // null on hard failure
    QList<DiagnosticMessage> diagnostics;             // warnings + errors surfaced to the tab indicator
};
```

`load()` runs on a worker thread — `AppController::openFile()` dispatches it via `QtConcurrent::run()`, and the worker moves the session's models to the controller's thread before the result is published. The controller wraps the session in a `DocumentTab`, which owns it and the filter over its tree. Expect to be called with an absolute path; let parser-layer errors flow into `diagnostics` instead of throwing. Do not depend on the GUI event loop inside `load()` or a session constructor: application shutdown sets `cancel` and waits for the pending load on the GUI thread. Hand `cancel` to a parser that can stop early (the MDF4 reader observes it while opening); an adapter whose parser cannot stop takes the parameter unnamed, and shutdown waits for its parse to end. The adapter is owned by the controller's `FormatList` and outlives every load it runs.

Format identity for the file dialog and suffix lookup comes from the `FormatEntry`; `formatDisplayName(FormatId)` labels the dialog filter. The session reports its own identity (`formatId()`, `formatName()`) for tabs.

## DocumentSession interface

```cpp
class DocumentSession {
public:
    virtual ~DocumentSession() = default;
    virtual FormatId formatId() const = 0;
    virtual QString formatName() const = 0;
    virtual QString displayName() const = 0;          // tab label
    virtual QString sourcePath() const = 0;
    virtual TreeModel* treeModel() = 0;               // left NavPanel
    virtual DetailModel* detailModel() = 0;           // right Detail panel
    virtual QList<DiagnosticMessage> diagnostics() const = 0;
    virtual bool hasDiagnostics() const = 0;          // true when any diagnostic (warning or error) exists
    virtual void selectNode(quint64 key) = 0;         // show the entity of the row with this key

    // Optional center panel (memory view / signal map / blank)
    virtual QUrl centerPanelSource() const { return {}; }
    virtual QAbstractListModel* centerPanelModel() { return nullptr; }

    virtual void moveModelsToThread(QThread* thread) = 0;
};
```

`AdapterSessionBase` ([src/sessions/adaptersessionbase.h](../../src/sessions/adaptersessionbase.h)) provides identity, diagnostics, the tree and detail models, and row keys: `appendNode()` gives every row the session's next key. Use it as the base class unless your format genuinely needs to bypass it.

Declare the format's entity kinds and a typed path (`<Fmt>Path`) next to its presenter. The session owns the presenter by value and a table from the key of each entity row to its path, filled by an `appendEntity()` that calls `appendNode()`. `selectNode(key)` looks the key up and hands `DetailModel::setSelection()` the presenter's details and a producer of the entity's raw JSON, or no producer when the entity has no raw form. Its observers may select another row or close the session, so make it the last step, or recheck the session's lifetime and selection after it as the MDF4 session does ([MDF4 reads](architecture.md#mdf4-reads)).

## CMake wiring

Each format compiles into its own static backend library, `explorer-<fmt>-backend`. The parser links **into that backend**; `explorer-formats` links every backend and the executable links `explorer-formats`. `explorer-core` never links a backend or a parser.

```cmake
# Package mode resolves the producer export; source mode requires the
# same target from the workspace composition.
find_package(<fmt>parser CONFIG REQUIRED)

qt_add_library(explorer-<fmt>-backend STATIC
    src/adapters/<fmt>adapter.cpp
    src/sessions/<fmt>documentsession.cpp
)
target_link_libraries(explorer-<fmt>-backend
    PRIVATE
        Qt6::Core
        protobuf::libprotobuf
        explorer-core
        <fmt>parser::<fmt>parser # parser links INTO the backend
)

# Add the backend to explorer-formats' PRIVATE link list and to the
# AUTOMOC OFF set_target_properties() list.
```

Parser acquisition and selection are described in [the build reference](../ref/cmake_build_system.md).

## Center panel

If your format has nothing graphical to show in the middle column, leave `centerPanelSource()` returning the default empty `QUrl{}` — the layout falls back to two columns automatically. If you want a memory grid or signal map view, mirror the A2L or DBC/LDF sessions:

| Format | `centerPanelSource()` | Model |
|---|---|---|
| A2L | `qrc:/qt/qml/ExplorerApp/qml/components/MemoryView.qml` | `MemoryMapModel` |
| DBC, LDF | `qrc:/qt/qml/ExplorerApp/qml/components/SignalMapView.qml` | `SignalMapModel` |
| MDF4 | `qrc:/qt/qml/ExplorerApp/qml/components/SignalPlotView.qml` | `SignalPlotModel` |

The center views use `QQuickPaintedItem` C++ renderers. The signal grid is driven by
pre-computed per-bit arrays, the memory grid by byte queries over sorted object
intervals ([rendering](architecture.md#rendering)); the signal plot consumes the format-neutral
overview and exact windows of `src/models/plotdata.h`, which its builders make from
ordered sample chunks. Adding a new recording format that can deliver such chunks
needs no plot changes.

### Lazy bulk-data sessions

Keep metadata extraction inside `FormatAdapter::load()` so opening and browsing
a large recording does not read sample payloads. The session owns lazy work:

- translate its format document into tree and detail models at open, admitting the
  tree against a finite allowance before building it;
- scan a selected channel away from the GUI thread, feeding its chunks to a
  `PlotOverviewBuilder`, and scan the covers the plot's `detailRequest()` names into
  `PlotWindowBuilder`s;
- run one scan at a time, keep only the latest wanted one pending, cancel work a
  newer selection or view makes obsolete, let no cancelled result or stale progress
  reach the model, and release a cancelled result before its reservation and
  before the next scan is admitted;
- reserve each builder's bytes before launch and cache completed results within
  one allowance, evicting only results nobody else holds; and
- install results only after the scan completed successfully.

`Mdf4DocumentSession` is the reference: [MDF4 reads](architecture.md#mdf4-reads).

Decoder/library types must not appear under `src/models/`, `src/ui/`, or the
plot QML component. This keeps the plot reusable by future recording backends.

## Checklist

1. Add `FormatId::<FMT>` to `src/core/formatid.h`, with its `formatDisplayName()` label.
2. Write the adapter pair: `src/adapters/<fmt>adapter.{h,cpp}` implementing `load()`.
3. Write the session: `src/sessions/<fmt>documentsession.{h,cpp}` extending `AdapterSessionBase`. Implement `treeModel()`, `selectNode()`, and either a center-panel pair or leave the defaults.
4. Resolve the canonical parser target in `CMakeLists.txt` (the `AFF_PARSER_MODE` loop).
5. Add a static `explorer-<fmt>-backend` library carrying the adapter/session sources, link the parser plus `explorer-core` into it, and add it to `explorer-formats`.
6. Add the format's entry to `builtInFormats()` in `src/builtinformats.cpp`; the dialog filters and sample list follow from it. Extend `tests/tst_builtinformats.cpp` with its suffixes and a bundled sample.
7. Run the app, open a sample file (Ctrl+O or the NavPanel Open button), verify the tab opens and the tree populates.

Workspace source builds need no parser release; package builds consume the parser's complete install archive, as described in [the build reference](../ref/cmake_build_system.md#complete-installed-packages).
