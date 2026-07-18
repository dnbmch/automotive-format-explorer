# Backlog — automotive-format-explorer

Items deliberately deferred or pending. Each entry says **what**, **where**, and the rough size.

## Test coverage

### BL-E1: ctest registration

`CMakeLists.txt` has no `enable_testing()` and no `add_test(...)`; no `tests/` directory exists. Add a minimal QTest target that opens an empty registry and asserts `TreeModel.rowCount() == 0` after construction. Wire via `qt_add_executable` + `add_test`.

**Size:** S — one tests/ subdir, one .cpp, one CMake fragment.

## Parked operator decisions

Structural findings from /enforce coverage, awaiting a call — deliberately not applied.

### BL-E2: adapter triplication

`src/adapters/a2ladapter.cpp` / `dbcadapter.cpp` / `ldfadapter.cpp` are ~95% identical (70/70/66 lines). The finder judged a shared abstraction premature at three near-clones; revisit if a fourth format lands or the clones drift.

**Size:** M — only if unified; the status quo is a deliberate keep.
