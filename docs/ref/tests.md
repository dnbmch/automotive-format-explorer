# Tests

The QTest suites in `tests/` are registered with CTest (`tests/CMakeLists.txt`);
`release-explorer.sh` runs them on both platforms before a release. The Python-driven
build tests are in [build-tree samples](cmake_build_system.md#build-tree-samples) and
[the Linux package](release_packaging.md#the-linux-package).

## Coverage

Tree filtering, memory models, the plot builders (bin partition up to 2^64, domain
decision, exact windows over gaps, repeats and nonfinite values, storage), the plot
model and painted item, A2L/MDF4 detail presenters, MDF4 scan scheduling and
cancellation, stale progress and results, outcomes, reservation and eviction, tree
admission, worker-thread hand-off and teardown from and during scans, a generated
4.5 M-sample recording through the real reader, the production format list (suffixes,
dialog filters, sample classification, one bundled sample per format opened through
the controller), controller load/shutdown lifetimes and the tab contract through a
fake adapter, tab navigation and pre-filter snapshots, lazy raw JSON, the painted
items' model lifetimes, and the production nav panel offscreen.

The end-to-end writer-file smoke (`tst_mdf4writerfile`) opens and plots the bundled
`samples/demo_recording.mf4` through the production adapter, and a truncated copy asks
for a reload; `MDF4_WRITER_SAMPLE` points it at a different recording, and it reports
as a CTest skip when that resolves to nothing.

## Harness facts

- `tst_navpanel`, `tst_signalplotview` and `tst_valuetableview` load the production
  QML from a test-time `ExplorerApp` module that `tests/CMakeLists.txt` assembles; a
  new import or singleton of `NavPanel.qml`, `SignalPlotView.qml` or
  `ValueTableView.qml` must be added there.
- The painted-item suites compile their item's source themselves (the items belong to
  the executable, not a library) and run with `QT_QPA_PLATFORM=offscreen`. Pixel
  checks compare 8-bit RGB; a `QColor` keeps finer components than the image stores.
- The offscreen platform finds no font directory and falls back to a 10 px sans
  serif that measures wider than the real fonts, so a width assertion states the rule
  (fits, or sits at its cap), never a pixel count.
- `tst_appcontroller` and `tst_mdf4documentsession` synchronise on
  `QThreadPool::globalInstance()->waitForDone()`; the session suite's `settle()` runs
  the pool dry repeatedly, because a completion starts the next scan. Unrelated pool
  work left running, or a blocked read, makes that wait cover it too.
- `QPromise` throttles progress callouts itself (about 25 per second), which hides a
  missing worker-side interval in short tests; `progressIsCoalesced` runs over a
  second to tell them apart.
- A `var` property written from C++ (`setProperty`) keeps its value when the QObject
  is destroyed; one assigned in QML reads null. A pending nav panel restore detects a
  destroyed tab by its missing `treeModel`.
- On Windows, `qt_standard_project_setup()` emits every executable, tests included,
  into the build root. Any script CMake or Ninja invokes must pin its own msys
  runtime's tools first ([runtime provenance](cmake_build_system.md#runtime-provenance-on-windows)).
