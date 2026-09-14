# Adapter contract

How to add another format to the explorer.

The explorer is plugin-based at the architecture level: each format ships an adapter (loads a file → returns a session) and a session (owns the parsed document and exposes models to QML). On Windows adapters are shared libraries loaded by `QLibrary` at runtime; on Linux they are linked statically into the executable.

## What you need to write

| File | Purpose |
|---|---|
| `src/adapters/<fmt>adapter.h` + `.cpp` | `class <Fmt>Adapter final : public FormatAdapter` + `extern "C" FormatAdapter* create<Fmt>AdapterPlugin()` factory |
| `src/sessions/<fmt>documentsession.h` + `.cpp` | `class <Fmt>DocumentSession : public AdapterSessionBase` (or directly `DocumentSession`) — owns the parsed proto document, builds the `TreeModel`, populates the `DetailModel` per node click, optionally exposes a center-panel model |
| Canonical parser target in `CMakeLists.txt` | Supplies matched headers and static library from source composition or an installed package |
| Registration in `src/core/appcontroller.cpp` static block | Under `#ifdef BACKENDS_STATIC` (Linux), call `_format_registry.registerAdapter(std::unique_ptr<FormatAdapter>(create<Fmt>AdapterPlugin()))` |

The four existing implementations under `src/adapters/` and `src/sessions/`
are the working references. DBC is the smallest metadata-at-open template;
MDF4 is the reference for metadata-only open followed by lazy bulk-data work.

## FormatAdapter interface

```cpp
class FormatAdapter {
public:
    virtual ~FormatAdapter() = default;
    virtual FormatId formatId() const = 0;            // enum value in src/core/formatid.h
    virtual QString formatName() const = 0;           // human-readable, e.g. "DBC"
    virtual QStringList extensions() const = 0;       // e.g. {"dbc"} — matched case-insensitively
    virtual LoadResult load(const QString& path) const = 0;
};

struct LoadResult {
    std::unique_ptr<DocumentSession> session;         // null on hard failure
    QList<DiagnosticMessage> diagnostics;             // warnings + errors surfaced to the tab indicator
};
```

`load()` runs on a worker thread — `AppController::openFile()` dispatches it via `QtConcurrent::run()` and moves the resulting models back to the main thread on completion. Expect to be called with an absolute path; let parser-layer errors flow into `diagnostics` instead of throwing.

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

## Plugin entry point

Each adapter exposes a single `extern "C"` factory:

```cpp
// in src/adapters/<fmt>adapter.cpp
extern "C" FormatAdapter* create<Fmt>AdapterPlugin() {
    return new <Fmt>Adapter();
}
```

On Windows (`.dll` plugin discovery) `QLibrary::resolve()` looks up this exact symbol. On Linux the same function is linked into the binary and called directly from the static block in `AppController::AppController()`.

## CMake wiring

Each format compiles into its own backend library — `explorer-<fmt>-backend`, built `SHARED` on Windows and `STATIC` on Linux via `${_backend_lib_type}`. The parser lib links **into that backend**, never into the `automotive-format-explorer` exe directly. The exe pulls backends in differently per platform: `add_dependencies` on Windows (the `.dll` is loaded at runtime by `QLibrary`), a direct static link on Linux.

```cmake
# Package mode resolves the producer export; source mode requires the
# same target from the workspace composition.
find_package(<fmt>parser CONFIG REQUIRED)

# Per-format backend library — built SHARED (Windows) or STATIC (Linux)
qt_add_library(explorer-<fmt>-backend ${_backend_lib_type}
    src/adapters/<fmt>adapter.cpp
    src/sessions/<fmt>documentsession.cpp
)
target_include_directories(explorer-<fmt>-backend PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(explorer-<fmt>-backend
    PRIVATE
        Qt6::Core
        protobuf::libprotobuf
        explorer-core
        <fmt>parser::<fmt>parser # parser links INTO the backend, not the exe
)
set_target_properties(explorer-<fmt>-backend PROPERTIES
    AUTOMOC OFF
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

# Wire the backend into the exe — per platform
if(WIN32)
    add_dependencies(automotive-format-explorer explorer-<fmt>-backend)
else()
    target_link_libraries(automotive-format-explorer PRIVATE explorer-<fmt>-backend)
endif()
```

Parser acquisition and selection are described in [the build reference](../ref/cmake_build_system.md). On Linux, `BACKENDS_STATIC` is defined on the exe and the `create<Fmt>AdapterPlugin()` factory is registered at startup; on Windows the backend `.dll` is discovered and loaded lazily on first open.

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

1. Add `FormatId::<FMT>` to `src/core/formatid.h`.
2. Write the adapter pair: `src/adapters/<fmt>adapter.{h,cpp}` with the `extern "C"` factory.
3. Write the session: `src/sessions/<fmt>documentsession.{h,cpp}` extending `AdapterSessionBase`. Implement `treeModel()`, `selectNode()`, and either a center-panel pair or leave the defaults.
4. Call `fetch_parser_lib(TARGET <fmt>parser ...)` in `CMakeLists.txt`.
5. Add an `explorer-<fmt>-backend` library (`qt_add_library(... ${_backend_lib_type})`) carrying the new adapter/session sources, link the parser plus `explorer-core` into it, then wire it to the exe — `add_dependencies` on Windows, `target_link_libraries` on Linux.
6. Register the create-function in `AppController::AppController()` under `#ifdef BACKENDS_STATIC`.
7. Run the app, open a sample file (Ctrl+O or the NavPanel Open button), verify the tab opens and the tree populates.

The format's own parser library (under `dnbmch/<fmt>-parser-lib`) must publish a `v*` release with the renamed `<fmt>parser-*` artifact name before the explorer can fetch it.
