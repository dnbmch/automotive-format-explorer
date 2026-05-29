# Adapter contract

How to add a fourth format to the explorer.

The explorer is plugin-based at the architecture level: each format ships an adapter (loads a file → returns a session) and a session (owns the parsed document and exposes models to QML). On Windows adapters are shared libraries loaded by `QLibrary` at runtime; on Linux they are linked statically into the executable.

## What you need to write

| File | Purpose |
|---|---|
| `src/adapters/<fmt>adapter.h` + `.cpp` | `class <Fmt>Adapter final : public FormatAdapter` + `extern "C" FormatAdapter* create<Fmt>AdapterPlugin()` factory |
| `src/sessions/<fmt>documentsession.h` + `.cpp` | `class <Fmt>DocumentSession : public AdapterSessionBase` (or directly `DocumentSession`) — owns the parsed proto document, builds the `TreeModel`, populates the `DetailModel` per node click, optionally exposes a center-panel model |
| `cmake/FetchParserLib.cmake` call in `CMakeLists.txt` | Resolves the parser headers + static lib from the public `-lib` release artifacts |
| Registration in `src/core/appcontroller.cpp` static block | Under `#ifdef BACKENDS_STATIC` (Linux), call `_format_registry.registerAdapter(std::unique_ptr<FormatAdapter>(create<Fmt>AdapterPlugin()))` |

The three existing implementations under `src/adapters/{a2l,dbc,ldf}adapter.*` and `src/sessions/{a2l,dbc,ldf}documentsession.*` are the working reference. DBC is the smallest and is the cleanest template.

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
    virtual bool hasWarnings() const = 0;             // drives the tab ⚠ indicator
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

```cmake
# CMakeLists.txt — call once per parser dependency
fetch_parser_lib(
    TARGET <fmt>parser
    VERSION ${<FMT>_PARSER_VERSION}   # e.g. v0.2.0
)
target_link_libraries(automotive-format-explorer PRIVATE <fmt>parser)

# add the new source files to target_sources(automotive-format-explorer ...)
# under both Windows (SHARED) and Linux (BACKENDS_STATIC) branches
```

`fetch_parser_lib` is implemented in [cmake/FetchParserLib.cmake](../../cmake/FetchParserLib.cmake). It downloads the renamed `<fmt>parser-*` release artifact from the public `-lib` repo's GitHub Releases at configure time.

## Center panel

If your format has nothing graphical to show in the middle column, leave `centerPanelSource()` returning the default empty `QUrl{}` — the layout falls back to two columns automatically. If you want a memory grid or signal map view, mirror the A2L or DBC/LDF sessions:

| Format | `centerPanelSource()` | Model |
|---|---|---|
| A2L | `qrc:/qt/qml/ExplorerApp/qml/components/MemoryView.qml` | `MemoryMapModel` |
| DBC, LDF | `qrc:/qt/qml/ExplorerApp/qml/components/SignalMapView.qml` | `SignalMapModel` |

Both views are `QQuickPaintedItem` C++ renderers driven by pre-computed flat arrays — adding a new view means another component in [qml/components/](../../qml/components/) plus a `QQuickPaintedItem` subclass in [src/ui/](../../src/ui/).

## Checklist

1. Add `FormatId::<FMT>` to `src/core/formatid.h`.
2. Write the adapter pair: `src/adapters/<fmt>adapter.{h,cpp}` with the `extern "C"` factory.
3. Write the session: `src/sessions/<fmt>documentsession.{h,cpp}` extending `AdapterSessionBase`. Implement `treeModel()`, `selectNode()`, and either a center-panel pair or leave the defaults.
4. Call `fetch_parser_lib(TARGET <fmt>parser ...)` in `CMakeLists.txt` and link it.
5. Add the new source files to `target_sources(automotive-format-explorer ...)` for both Windows shared-library and Linux `BACKENDS_STATIC` branches.
6. Register the create-function in `AppController::AppController()` under `#ifdef BACKENDS_STATIC`.
7. Run the app, drag in a sample file, verify the tab opens and the tree populates.

The format's own parser library (under `dnbmch/<fmt>-parser-lib`) must publish a `v*` release with the renamed `<fmt>parser-*` artifact name before the explorer can fetch it.
