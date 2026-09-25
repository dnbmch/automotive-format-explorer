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
    virtual LoadResult load(const QString& path) const = 0;
};

struct LoadResult {
    std::unique_ptr<DocumentSession> session;         // null on hard failure
    QList<DiagnosticMessage> diagnostics;             // warnings + errors surfaced to the tab indicator
};
```

`load()` runs on a worker thread — `AppController::openFile()` dispatches it via `QtConcurrent::run()`, and the worker moves the session's models to the controller's thread before the result is published. Expect to be called with an absolute path; let parser-layer errors flow into `diagnostics` instead of throwing. Do not depend on the GUI event loop inside `load()` or a session constructor: application shutdown waits for a pending load on the GUI thread. The adapter is owned by the controller's `FormatList` and outlives every load it runs.

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
    virtual void selectNode(quint64 key) = 0;         // refresh DetailModel for a NodeRegistry key

    // Optional center panel (memory view / signal map / blank)
    virtual QUrl centerPanelSource() const { return {}; }
    virtual QAbstractListModel* centerPanelModel() { return nullptr; }

    virtual void moveModelsToThread(QThread* thread) = 0;
};
```

`AdapterSessionBase` ([src/sessions/adaptersessionbase.h](../../src/sessions/adaptersessionbase.h)) provides the model plumbing and node-registry handling. Use it as the base class unless your format genuinely needs to bypass it.

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

The center views use `QQuickPaintedItem` C++ renderers. Grid views are driven by
pre-computed flat occupancy arrays; the signal plot uses a format-neutral
`PlotSeries` seam and min/max summaries. Adding a new recording format that can
produce `PlotSeries` needs no plot changes.

### Lazy bulk-data sessions

Keep metadata extraction inside `FormatAdapter::load()` so opening and browsing
a large recording does not read sample payloads. The session owns lazy work:

- translate its format document into tree and detail models at open;
- translate a selected channel into `PlotSeries` at the plot seam;
- dispatch sample decoding away from the GUI thread and pass an explicit range;
- cache completed channels; and
- tag each request so a result from an older selection cannot update the model.

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

Workspace source builds need no parser release; package builds consume the parser's complete install archive, selected as described in [the build reference](../ref/cmake_build_system.md#acquire-complete-installed-packages).
