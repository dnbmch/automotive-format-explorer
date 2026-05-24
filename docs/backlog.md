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

### BL-E4: `docs/ref/memory_view.md` mixes current behaviour with unbuilt design

Several sections describe features that are not implemented:

- Lines 96, 99-106: hatched/striped overlap pattern + half-filled bit-mask cells in the memory grid. Only `SignalGridItem::paint` (`src/ui/signalgriditem.cpp:93-103`) implements stripes; `MemoryGridItem` does not.
- Lines 135-140: click-drag byte-range selection + "Selected: 0x… N objects in range" status. `MemoryGridItem` (`src/ui/memorygriditem.cpp:274-291`) implements single-click selection only.
- Lines 136-137: disambiguation popup — no UI exists in `qml/components/NavPanel.qml` or anywhere else.
- Lines 99-106: tooltip mock with "Record Layout / Conversion" fields. Actual `MemoryGridItem::hoveredTooltip` (`src/ui/memorygriditem.cpp:185-203`) emits name / type / address / size only.

Move the aspirational sections out of `docs/ref/` (which is for current behaviour) to either a new `docs/spec/memory_view_planned.md` or directly into this backlog entry so `docs/ref/memory_view.md` describes only what exists today.

**Size:** S — doc-only, no code changes.

## Refactor

### BL-E5: Split `src/sessions/a2ldocumentsession.cpp` (~2300 LOC)

The file accreted construction, query, and rendering-helper responsibilities and is by a wide margin the largest source file in the repo. Split into 2-3 files along clear concern seams (e.g. construction vs query vs rendering helpers).

No ctest target exists in this repo yet (`BL-E1` covers that). Any split should be paired with at least a smoke build plus a manual run-through of the existing A2L fixtures to catch regressions, since there is no automated safety net.

Prefer folding the split into the next feature change that would have touched this file rather than running a standalone refactor.

**Size:** L if standalone, S-M if folded into the next in-flight change.

### BL-E6: `SignalGridItem` overlap stripe rendering subsection

`src/ui/signalgriditem.cpp:93-103` implements the overlap-stripe pattern (and the half-filled bit-mask cells implied by it). `docs/arch/architecture.md` mentions stripes only in the high-level "Rendering" section — there is no diagram or per-color rule explaining when each stripe pattern is drawn.

Add a short subsection (architecture.md or a dedicated `docs/ref/signal_grid_rendering.md`) covering the rules and one example diagram. Defer until the rendering changes again so the doc and the code land together.

**Size:** XS.

### BL-E7: `SignalGridItem` keyboard navigation reference

Tab, arrows, Home, End, PageUp, and PageDown are handled in `src/ui/signalgriditem.cpp` but undocumented. Add a "Keyboard navigation" block to `docs/ref/signal_map.md` (and mirror in `docs/ref/memory_view.md` if `MemoryGridItem` shares the same bindings) listing each key, the focus model, and how the selection updates the detail panel.

**Size:** XS.