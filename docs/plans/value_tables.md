# Value tables for text and array channels (BL-V5, BL-V6)

Locked plan. A decodable text channel shows its samples as a paged table (index,
time, text); a decodable array channel shows one sample as a matrix with axis
headers. Both read through the reader's text and array lanes, run on the session's
single-flight scheduler and live within the session's result allowance. The plot
stack (`plotdata`, `SignalPlotModel`, `SignalPlotItem`, `SignalPlotView.qml`) is not
touched.

Operator decisions: the selection switches the session's center view and the
controller announces it (1a); the text table moves in pages of 1,024 samples, one
bounded read each (2a); a cell keeps at most 1 KiB of UTF-8 (3a). The axis-sample
pairing is the spec's common-master rule (`backlog.md` BL-V6; `mdf4.proto`
`ArrayDimension` comment).

## Center panel

- `DocumentSession::centerPanelModel()` returns `QAbstractItemModel*` (the plot model
  is a list model, the value table a table model). `AppController::centerPanelModel`
  and the `main.cpp` registration follow; the suites that cast the model to the plot
  keep working (`qobject_cast`).
- `Mdf4DocumentSession` keeps `_center` (Plot or Table). `centerPanelSource()` and
  `centerPanelModel()` follow it: `SignalPlotView.qml` with `_plot_model`, or
  `ValueTableView.qml` with `_table_model`. Selecting anything that is not a
  decodable text or array channel returns the center to the plot.
- `AppController` gains `centerPanelChanged`, the NOTIFY of `centerPanelSource` and
  `centerPanelModel`. `announceCenterPanel()` compares (tab, source, model) with the
  last announced triple and emits once when it differs; it runs at the end of
  `announceCurrentTab()` (after `currentSessionChanged`) and after `selectNode()` in
  `selectCurrentNode()`. It observes `_shut_down` like the other announcements; a
  nested change announces itself and the outer call then finds nothing to announce.
- `Main.qml`'s center `Loader` reloads on `onCenterPanelChanged` instead of
  `onCurrentSessionChanged`. Closing an earlier tab emits neither signal, so the
  current plot keeps its zoom. A tab switch rebuilds the center the session last
  chose; the table model keeps its channel, page, sample and slice.

## Session

- Construction takes one `Mdf4Source { scan, scanText, readArray, axis }` in place
  of the two function arguments; `Mdf4Adapter::load()` binds all four to the one
  `mdf4::Reader`, the session suite's fake source provides all four over its one
  gate, cancel check, `available` and request log. The function shapes are the
  reader's: `scanText(g, c, first, count, control, TextVisitor)`,
  `readArray(g, c, first, count, ElementWindow, control)`.
- `selectChannel()` forks on `decodable && (textual || has_array)` to
  `selectTable()`. A master keeps its note, a non-decodable channel "This channel
  type is not plottable". The plot notes "Text channel — not plottable" and "Array
  channel — not plottable" go.
- `selectTable()` settles first (`_selected`, `_center = Table`, the wanted request:
  text page 0 or array sample 0 with slice 0…0), installs a cached result at once,
  keeps the same read already in flight, otherwise cancels the active read and sets
  `_pending`; then notifies in order, checking the `QPointer` and the selection count
  after each step as today: `_plot_model->clear()`, `_table_model->setChannel(...)`,
  `startPending()`, `updateBusy()`.
- `Request`, `Completion` and `Retained` gain the table kinds (text page keyed by
  channel and first sample; array sample keyed by channel, sample and element
  window). `held()`, `admit()`, eviction, `retain()`, `takeResult()`, the watcher,
  `moveModelsToThread` and the destructor treat them exactly like plot results: one
  `_active`, one `_pending`, results held while shown, evicted LRU when not. The
  refusal wording says "result memory" in place of "plot memory".
- Selecting a numeric channel mid-read cancels the table read; its completion is
  taken and released before the pending overview starts. Changing the page, sample
  or slice mid-read cancels the read unless it is the same request. A model request
  (page, sample, slice) reaches the session the way the plot model's window request
  does.

## Text table

- A page is 1,024 samples aligned on multiples of 1,024 (`first = n - n % 1024`); one
  `scanText(g, c, first, 1024, control, visitor)` per page, the visitor always
  `Continue`. Rows are the clamped samples read (`ScanResult.firstSample`,
  `samplesRead`); an unsorted group's short read shows what arrived.
- `TextPageBuilder(first, rows)` in `src/models/tabledata.{h,cpp}`:
  `reservation(rows) = sizeof(builder) + sizeof(TextPage) + rows × (8 B time + 4 B
  end offset + 1,024 B cell cap)`, admitted before launch. Each text is cut at 1,024
  UTF-8 bytes on a character boundary and marked with "…". `finish()` shrinks the
  arena; `TextPage::bytes()` never exceeds the reservation.
- An invalid sample arrives as an empty text (`reader.h` TextChunk) and shows as an
  empty cell; the footer counts `invalidValues`.

## Array table

- `src/sessions/mdf4arrayread.{h,cpp}`, Qt-free, namespace `mdf4array`, its own
  function aliases of the reader's shapes (`ScanFunction`, `ReadArrayFunction`,
  `AxisFunction`):
  - `ArrayPlan arrayPlan(const mdf4::File&, const AxisFunction&, group, channel,
    sample, const std::vector<std::uint64_t>& slice)` on the GUI thread from
    metadata only. Dimensions outer first; D ≥ 2: rows = N(D−1), columns = N(D),
    `slice` holds the D−2 outer indices (clamped), the element window is
    `{flat(slice) × rows × columns, rows × columns}` because k is row-major outer
    first (`reader.h` ElementWindow, `mdf4.proto` ChannelArray); D = 1: N rows × 1
    column. Fields: `group, channel, sample, slice, rows, columns, window, rowAxis,
    columnAxis`.
  - `AxisSource { Kind kind (Index, Fixed, Channel); std::vector<double> fixed;
    std::uint32_t group, channel; bool intervals; bool leftOpen; AxisPairing
    pairing (Same, Constant, CommonMaster, None); std::uint64_t labels; }`. Fixed:
    `axis_values`. Channel: the axis channel at (`axis_group`, `axis_channel`) is
    itself a one-dimensional array channel (`ca_type` 1: the first N elements;
    `ca_type` 3: N + 1 borders, `intervals`, `leftOpen` from its own
    `Channel.array.left_open`); it is read through `readArray`, never `scan`.
    Pairing: `Same` when the axis group is the array's group (j = s); `Constant`
    when the axis group has one cycle and `axis(ag).kind` is not `Master` (j = 0);
    `CommonMaster` when both `axis(g)` and `axis(ag)` are `Master`; `None`
    otherwise (index headers, note "no common master with <axis>").
  - `std::uint64_t reservation(const ArrayPlan&)`: `sizeof(ArraySample) + 8 × (rows ×
    columns + 1 + row labels + column labels)`, saturating.
  - `ArraySample readArraySample(const ArrayPlan&, const ReadArrayFunction&, const
    ScanFunction&, const mdf4::Control&)` on the worker: `readArray(g, c, s, 1,
    window)` gives the values and t_s (Ok with `samplesRead == 0` when s is past the
    recording). Then per channel axis: `CommonMaster` scans the axis group's master
    channel (`axis(ag).group/channel`) over its cycle count with `scan`, keeping the
    last index whose time ≤ t_s (ties to the latest); none → index headers with the
    note "no <axis> sample at or before this one". Then `readArray(ag, ac, j, 1, {0,
    labels})`. An axis read that fails other than `Cancelled` or `SourceChanged`
    gives index headers with its message as the note; the values still show. A
    `Cancelled` or `SourceChanged` outcome anywhere is the sample's outcome.
  - `ArraySample { mdf4::Outcome outcome; std::uint64_t samplesRead; double time;
    std::uint64_t rows, columns; std::vector<double> values (row-major, NaN =
    invalid); AxisLabels rowLabels, columnLabels; std::uint64_t bytes() const; }`,
    `AxisLabels { Kind kind (Index, Points, Intervals); std::vector<double> values;
    bool leftOpen; std::string note; }`. The reader's vectors are moved in, so
    `bytes()` stays within the reservation.
- The model clamps `showSample` to [0, stated − 1]; an Ok read with no sample shows
  "No sample s: the recording ends after M samples". A slice the reader refuses
  (above `readValues`) shows the refusal note.

## Model and view

- `ValueTableModel : QAbstractTableModel` in explorer-core
  (`src/models/valuetablemodel.{h,cpp}`), one class for both kinds, registered
  uncreatable in `main.cpp`. Text: rows = the page's samples, columns Time and Text
  (Text alone when the group's axis is not `Master`), vertical header = sample index.
  Array: rows × columns = the slice, vertical header = row labels, horizontal header
  = column labels. `DisplayRole` strings: NaN → "", ±inf → "∞" / "−∞", values and
  labels `'g'` 7 significant digits, time `'g'` 12; interval labels `[a, b[`, `]a,
  b]` when left-open; index headers the zero-based index.
- Properties for the view: kind, channel name, role chip (the tree's role text:
  "Text channel", or the array type and shape from the presenter), unit, stated
  sample count, state (as the plot model names them: pending, shown, empty, refused),
  message, progress; text: first row and rows shown ("Rows a–b of N"), invalid count;
  array: sample, its time, dimension sizes, slice. Invokables: `showSample(s)`,
  `nextPage()`, `previousPage()`, `setSlice(d, i)`; each emits a request the session
  serves. While a new page or sample is pending the previous rows are cleared. The
  model's reset follows `DetailModel`'s pattern so it survives its own destruction
  from a notification.
- `qml/components/ValueTableView.qml` (added to `QML_FILES`): `required property var
  mapModel`, `signal nodeKeyClicked`, `scrollToNodeKey()` no-op (the center-view
  contract); a header (name, chips, count, time for arrays, busy indicator and
  percentage); `TableView` with `HorizontalHeaderView` and `VerticalHeaderView`
  (QtQuick.Controls), the text column filling the width and eliding; a sample field
  with previous/next page (text) or previous/next sample (array); one index spin box
  per outer dimension; a footer with the note or the row range and corner label
  "rows \ columns" naming the two axes.

## Tests

Fakes: the session suite's fake source gains a text lane (text of sample i =
`"text i"`, time i) and an array lane (value(s, k) = 1000·s + k, chosen elements NaN)
sharing its gate, cancel check, `available` and request log. Real files: a new
`tst_mdf4tablecorpus` following `tst_mdf4writerfile`'s environment-variable and
exit-77 pattern with `MDF4_EXAMPLES=${PROJECT_SOURCE_DIR}/../a2l-parser/docs/ASAM_2022_04_07/2022_04_07/Measurement & Calibration/ASAM_MDF_BS_V4-2-0/BaseStandard/Examples`.
Every new case is seen failing first.

| # | Behaviour | Fixture | Assertion | Suite |
|---|---|---|---|---|
| 1 | Text page shown (replaces `textChannelScansNothing`) | text document, 4 samples | source ends in `ValueTableView.qml`; 4 rows; header(0) "0", (0,0) "0", (0,1) "text 0"; one text request `{0, 1024}`; results 1, held 1 | tst_mdf4documentsession |
| 2 | Bounded pages | 3,000 samples | `showSample(2500)` requests `{2048, 1024}`; 952 rows; header(0) "2048" | same |
| 3 | Reservation becomes the charge | gated text read | gated: reserved == `TextPageBuilder::reservation(1024)`; after: retained == `bytes()` ≤ reservation, reserved 0 | same |
| 4 | Text then numeric mid-read | gated text, then Engine | center is the plot; the text scan returns before Engine's request; no page retained; `maxConcurrent == 1` | same |
| 5 | Refused by the allowance | `resultBytes = reservation(1024) − 1` | refused; message starts "Texts need " and contains "but results in use hold"; no request; reserved 0 | same |
| 6 | Cached page | text → Engine → text | no second text request | same |
| 7 | Array sample shown (replaces `arrayChannelScansNothing`) | 52×142 array document | request `{s 0, window {0, 7384}}`; 52 rows, 142 columns; (1,0) "142" | same |
| 8 | Sample past the end | 4 samples, `available = 2` | `showSample(10)` requests sample 3; empty with an "ends after" message | same |
| 9 | NaN cell | NaN at k = 5 | (0,5) "", (0,4) "4" | same |
| 10 | Slice selector | 2×3×4 document | `setSlice(0, 1)` requests window `{12, 12}`; 3 rows × 4 columns | same, and tst_mdf4arrayread on the plan |
| 11 | Axis rule | a one-cycle masterless interval axis (left-open); a same-group axis; a common-master axis group with times 0.5 / 1.5 / 2.5 | interval axis read at sample 0 for s = 0 and 3, labels "]0, 1]"; same-group axis read at j = s; common master: s = 0 index headers with note, s = 1 reads j = 0, s = 3 reads j = 2 | tst_mdf4arrayread (fakes), then the session |
| 12 | Shutdown during a table read | `SessionAdapter` + `AppController`, gated text read | the scan returns before the model is destroyed; no timeout | same |
| 13 | Close cancels a table read | gated array read, `session.reset()` | events: scan returned, source released | same |
| 14 | Reentry from table notifications | observer of the model's reset selects Engine or closes the session | Engine shown; nothing touches a destroyed session | same |
| 15 | Controller announcement | `FakeSession` switching between two (URL, model) pairs | the switch emits `centerPanelChanged` once; a same-center selection none; a tab switch one; closing an earlier tab none; after `shutdown()` none | tst_appcontroller |
| 16 | Model formatting | unit | NaN "", ±inf, 3698.800048828125 → "3698.8"; "[−∞, 0[" and "]−∞, 0]"; a 1,025-byte text cut on a character boundary ending "…"; `showSample` clamps; survives destruction from a reset | tst_valuetablemodel (new) |
| 17 | View loads | test-time module like `tst_signalplotview` | text and array content offscreen without a QML warning; "zero" visible | tst_valuetableview (new) |
| 18 | Real text | `ChannelTypes/VLSD/Vector_VLSDStringUTF8.mf4` g0 c1 | 10 rows; row 0 "0" / "zero", row 9 "nine" | tst_mdf4tablecorpus (new) |
| 19 | Real unsorted text | `UnsortedData/VLSD/Vector_Unsorted_VLSD.MF4` g1 c1 | 140 rows; first "1 Rekorder mit Aufzeichnung gestartet", last "Messung gestoppt" | same |
| 20 | Classification | `Arrays/Classification/Porsche_2D_classification_result.mf4` `2D_classification` | 52×142; (1,0) "3698.8", (26,0) "10697.06"; both headers intervals starting "[−∞" | same, and tst_mdf4arrayread |
| 21 | Fixed axis and a channel axis before its first sample | `Arrays/Simple/Vector_MeasurementArrays.mf4` KF4 s = 0 | row headers "0"…"5"; column headers indices with the note (Curve1's first sample at 0.771 s is after KF4's 0.661 s); (2,4) "24" | same |
| 22 | Common-master pairing | same file, KF2 s = 3 | columns labelled by Curve2 sample 2 (11…22), not sample 3 | tst_mdf4arrayread |
| 23 | Slices on real data | `Arrays/Events/Trigger` dSPACE `Signal_MatrixDim` 2×3×4 (the file the parser's `corpus_array.cpp` uses) | slice 0 (0,1) "0.01"; slice 1 (0,0) "1"; sample 4 slice 1 (2,3) "1.234" | tst_mdf4tablecorpus |

## Packets

- **A — center switch, model, view, text table** (one implementer, build directory
  `C:\Projects\automotive-file-formats\build-tables-explorer`): everything above
  except `mdf4arrayread` and the array selection; `Mdf4Source` with all four
  functions; cases 1–6, 12–19 (12–14 on the text lane); docs below for the text
  table. Must not create or edit `src/sessions/mdf4arrayread.*` or
  `tests/tst_mdf4arrayread.cpp`.
- **B — array read** (one implementer, standalone build in the scratchpad against
  the installed reader in `build-arrays-parser-prefix`, the reviewer's
  `build-array-independent-review/CMakeLists.txt` as the pattern): only
  `src/sessions/mdf4arrayread.{h,cpp}` and `tests/tst_mdf4arrayread.cpp`; cases 10,
  11, 20, 22 at the plan and worker level, with fakes and with the corpus. Touches no
  other file.
- **C — array integration** (packet A's implementer, context intact, after A and B):
  `selectTable()` for arrays, the array kinds in the model and view, case 7–9, 11,
  20, 21, 23 in the session and corpus suites, B's files registered in
  `CMakeLists.txt` and `tests/CMakeLists.txt`, the array docs.

Done: every Explorer suite green in `build-tables-explorer` with 0 warnings, each new
case seen failing first; the lead's review; srv-one's `check.sh` on the pushed heads.

## Docs

New `docs/ref/value_table.md` (the table contract: center switch, pages and the
cell cap, the array slice and axis rule, accounting, states and notes). Update
`docs/arch/architecture.md` (process layout, tabs, the `DocumentSession` contract,
the MDF4 scheduling bullet), `docs/ref/signal_plot.md` (the text and array notes),
`README.md` (feature line), `docs/plans/mdf4_viewer.md`; remove BL-V5 and BL-V6 from
`docs/backlog.md`; the handoff's text and array operator-visual checks become table
checks. Harvest and archive this plan when the work lands.
