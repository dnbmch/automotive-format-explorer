# Table storage within its reservation; bounded slice controls

Locked plan. Two corrections to the value table ([value table](../ref/value_table.md)),
Explorer only, on the unchanged reader. The reader's API, the axis-pairing rule, the
1,024-sample text page, the 1 KiB cell cap and the single-flight scheduler stay as
they are.

## Findings

1. **Table reservations undercount construction storage.** The session admits a
   read's reservation against `Mdf4SessionLimits::resultBytes` as what the builder
   and its result hold at most (`tabledata.h` `TextPageBuilder::reservation`,
   `mdf4arrayread.h` `reservation`). Two paths hold more while they build:
   - `TextPageBuilder::add()` grows the text arena by doubling (`tabledata.cpp`, the
     `reserve` in `add`), so the old and the new buffer are alive together, and
     `finish()` shrinks every array, which again holds two buffers. A full page of
     1,024 texts of 1,024 Bytes reserves 1,060,976 Bytes and peaks at 1,585,256 live
     Bytes; its finished page holds 1,060,952, so `bytes() <= reservation` misses it.
   - `mdf4array::arrayPlan()` copies a fixed axis's points (`axis_values`) into
     `AxisSource::fixed` before admission; `Mdf4DocumentSession::launch()` copies the
     plan again for the task (`mdf4documentsession.cpp`, `ArrayPlan plan =
     request.plan`); `label()` copies them a third time into the result. For a
     one-dimensional sample of 65,536 elements with a fixed axis the reservation is
     1,048,856 Bytes and the live plan and result vectors reach 2,097,160.

   The plot builders have neither problem: `PlotOverviewBuilder` and
   `PlotWindowBuilder` reserve their arrays once and never shrink
   (`plotdata.cpp`), so an overview holds exactly its reservation and a window at
   most its own.

2. **Slice controls can take the whole table viewport.** `ValueTableView.qml`'s
   slice `Repeater` creates one `SpinBox` per outer dimension, singleton dimensions
   included, inside a wrapping `Flow` whose height the controls rectangle takes in
   full. A valid 1,728-Byte file with 128 dimensions of size 1 (the reader has no
   rank cap, by design) gives 126 spin boxes and a matrix of height 0 in a 700 × 400
   pane; 21 dimensions of size 2 leave 74 px of matrix at the 300 px minimum width.

## Storage: a builder's live storage is its reservation

- `TextPageBuilder` allocates `time` and `ends` for its rows and `utf8` for
  rows × `kTextCellBytes` at construction and never reallocates: `add()` only
  inserts (a cell never exceeds `kTextCellBytes`, the cut leaves room for the
  marker), `finish()` moves the page out without shrinking. `_arena_limit` goes.
  `reservation(rows)` keeps its formula; it now states what is allocated, the
  `make_shared` control block included (`sizeof(TextPageBuilder)` covers it). For
  every page of `rows`, delivered in full or short, `page->bytes() +
  sizeof(TextPageBuilder) == reservation(rows)`.
- `launch()` passes `rows` (`pageRows(stated, first)`) as the `scanText` count in
  place of `kTextPageSamples`, so the reader cannot deliver more rows than the
  builder allocated, whatever it clamps to.
- `AxisSource` holds a fixed axis as a pointer into the document, `const double*
  fixed` with `labels` as its count (`dimension.axis_values().data()`, contiguous),
  no vector. `arrayPlan()`'s contract: the plan refers into `document`, which must
  outlive it. `label()` makes the one accounted copy, `labels.values.assign(fixed,
  fixed + labels)`. In the session the task captures `_metadata` (the
  `shared_ptr<const mdf4::File>`) beside the source functions, so the document
  outlives the task by construction; `Active` already lives in the session that owns
  `_metadata`. The plan copy in `launch()` may stay: it holds no values any more.
- `mdf4array::reservation()`, `ArraySample::bytes()`, `ArraySlice::bytes()` and the
  session's admission, retention and eviction are unchanged.

## Slice controls: bounded, lazy, only where there is a choice

- `ValueTableView.qml` offers a selector only for an outer dimension (index < D − 2)
  of more than one element: `selectable`, the JS array of those dimension indices,
  computed from `mapModel.dimensions`.
- The `Flow` keeps the sample field and the two arrows. Below it, shown only while
  `selectable.length > 0`, one row holds the "Slice" label and a horizontal
  `ListView` of the selectors (`model: selectable.length`, lazy delegates, `clip`,
  `StopAtBounds`, a horizontal scroll bar that does not overlap the controls). Each
  delegate shows the dimension's number as the detail panel's Array card counts it
  (1-based) beside the `SpinBox`; `dimension = tableView.selectable[index]` binds
  `to`, `value` and `onValueModified: mapModel.setSlice(dimension, value)`; the
  tooltip stays.
- The controls rectangle's height is the `Flow`'s one line (at the 300 px minimum
  the sample controls fit one line) plus the strip's fixed row when shown, so it is
  bounded whatever the rank, and the matrix keeps the rest of the pane.
- The model's `dimensions`, `slice` and `setSlice()` are unchanged.

## Tests

Regressions fail on the current code and pass after; they are named for the rule.

- `tst_tablestorage` (new, Qt-free, plain `main`, registered with CTest beside the
  Explorer suites and linking `explorer-core`, `explorer-mdf4-backend`,
  `mdf4parser::mdf4parser` and protobuf): a replacement `operator new`/`delete` pair
  records the exact size of every allocation made through the executable's `operator
  new` in a side table and returns the allocator's pointer unchanged, so a foreign
  `delete` of such a pointer and a `delete` of a foreign pointer both stay correct;
  table overflow aborts the run. Inputs are built before each case's baseline. Each
  case prints reservation, retained and peak and fails when `peak − baseline >
  reservation`:
  - a full page: 1,024 texts of 1,024 Bytes (today 1,585,256 > 1,060,976);
  - the growth case: 1,024 texts of 700 Bytes (today the last doubling holds the
    358,400-Byte and the 716,800-Byte arena together, 1,087,592 against 1,060,976;
    the arena ends at exactly the texts' size, so nothing is compacted);
  - a short delivery: 3 texts into a 1,024-row builder, `page->bytes() +
    sizeof(TextPageBuilder) == reservation(1024)` (today the shrink makes it less);
  - an array sample: one dimension of 65,536 elements with a fixed axis of 65,536
    points, two plan owners as `Active` and the task hold them, a fake `readArray`
    that allocates the window's values and one time (today 2,097,160 > 1,048,856).
- `tst_valuetableview`: cases on the production QML at 700 × 400 and 300 × 400:
  - 128 dimensions of size 1, a 1 × 1 slice holding 42: no spin box, the cell "42"
    shown, the `TableView` at least 240 px high;
  - 21 dimensions of size 2, a 2 × 2 slice: fewer spin boxes instantiated than the
    19 selectable dimensions, at least one, the `TableView` at least 240 px high at
    300 px width, the cells shown;
  - dimensions {1, 5, 1, 3, 4}: exactly one spin box, `to` 4, its label "2";
    setting its value to 3 and emitting `valueModified` makes `wantedSlice()`
    {0, 3, 0} and emits `requestWanted`;
  - the existing 2 × 3 × 4 case keeps its one spin box; the text cases keep no strip.
  Any QML warning fails the case (`failOnWarning`, as today).
- Existing suites unchanged in intent: `tst_valuetablemodel`, `tst_mdf4arrayread`,
  `tst_mdf4documentsession`, `tst_mdf4tablecorpus` pass unchanged or with the
  equality the plan states.

## Packets

- **A — storage** (`src/models/tabledata.{h,cpp}`, `src/sessions/mdf4arrayread.{h,cpp}`,
  `src/sessions/mdf4documentsession.cpp` `launch()`, `tests/tst_tablestorage.cpp`,
  `tests/CMakeLists.txt`). Done: the four storage regressions seen failing, then
  passing; `tst_valuetablemodel`, `tst_mdf4arrayread`, `tst_mdf4documentsession`,
  `tst_mdf4tablecorpus` green; the Explorer suite green with 0 warnings.
- **B — slice controls** (`qml/components/ValueTableView.qml`,
  `tests/tst_valuetableview.cpp`). Done: the view regressions seen failing, then
  passing; `tst_valuetableview` green with 0 warnings.
- Docs (lead, after both land): `docs/ref/value_table.md` § Text pages, § Array
  samples, § Reads and storage; `docs/arch/architecture.md` § MDF4 reads Storage;
  the handoff. The plan is then archived.
