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

## Documentation

### BL-E4: `docs/ref/memory_view.md` mixes current behaviour with unbuilt design ✅ DONE

The unbuilt design (memory-grid overlap stripes + half-filled bit-mask cells,
rich tooltip record-layout/conversion fields, click-drag byte-range selection,
disambiguation popup, sub-byte subdivided cells, Tier 3 sizing, gap/overlap
detection, utilization, export, grid keyboard nav) moved to
[docs/plans/memory_view_planned.md](memory_view_planned.md). `docs/ref/memory_view.md`
now describes only current behaviour and links to the plan for the rest.

## Refactor

### BL-E5: Split `src/sessions/a2ldocumentsession.cpp` ✅ DONE

The detail-rendering logic was extracted into a `DetailPresenter` subclass:
`a2ldetailpresenter.{h,cpp}` + `a2ldetailpresenter_ifdata.cpp`, holding a
`const a2l::A2lFile&`. `a2ldocumentsession.cpp` is now 707 LOC and no source
file exceeds the soft cap. Committed; local build-verify pending with the rest
of the static-remediation work.

### BL-E6: `SignalGridItem` overlap stripe rendering subsection ✅ DONE

`docs/arch/architecture.md` "Rendering" now has an "Overlap stripes" subsection
documenting the `SignalMapModel::isOverlap(bit)` red diagonal hatch
(`src/ui/signalgriditem.cpp:93-103`), with an example diagram and a note that
`MemoryGridItem` does not draw stripes (planned).
