# Backlog — automotive-format-explorer

Items deliberately deferred or pending. Each entry says **what**, **where**, and the rough size.

## Test coverage

### BL-E1: ctest registration

`CMakeLists.txt` has no `enable_testing()` and no `add_test(...)`; no `tests/` directory exists. Add a minimal QTest target that opens an empty registry and asserts `TreeModel.rowCount() == 0` after construction. Wire via `qt_add_executable` + `add_test`.

**Size:** S — one tests/ subdir, one .cpp, one CMake fragment.

## Lifecycle hygiene

### BL-E2: QML engine vs controller declaration order

`src/main.cpp:33` registers `&controller` as a singleton instance with the QML engine. Engine teardown order on `QGuiApplication::exec()` return depends on declaration order in main; the controller currently survives the engine, which works today but is brittle. Move `QQmlApplicationEngine engine;` above `AppController controller;` so the engine is destroyed first.

**Size:** XS.