# Value table

The value table is the MDF4 session's center view for a channel whose samples are
not a signal to plot. A decodable text channel (`Channel.textual`) shows its samples
in pages, one row per sample with its index, time and text; a decodable array
channel (`Channel.array`) shows one sample at a time as a matrix with its axes as
headers. Both read through the reader's text and array lanes on the session's
single-flight scheduler and live within the session's result allowance
([MDF4 reads](../arch/architecture.md#mdf4-reads)).

`src/models/tabledata.h` holds the format-neutral table data (`TextPage`,
`TextPageBuilder`, `ArraySlice`); `ValueTableModel` (`src/models/valuetablemodel.h`,
explorer-core) is the view's `QAbstractTableModel`; `qml/components/ValueTableView.qml`
is the view. `src/sessions/mdf4arrayread.h` (namespace `mdf4array`, Qt-free) plans
and reads one array sample.

## Center switch

- `Mdf4DocumentSession` keeps its center: the plot, or the table while a decodable
  text or array channel is selected. `centerPanelSource()` and `centerPanelModel()`
  follow it: `SignalPlotView.qml` with the `SignalPlotModel`, or `ValueTableView.qml`
  with the `ValueTableModel`. Selecting anything else returns the center to the plot.
  The view that hands over is cleared, so it holds no result.
- `DocumentSession::centerPanelModel()` returns `QAbstractItemModel*`: the plot is a
  list model, the table a table model.
- `AppController::centerPanelChanged` is the notification of `centerPanelSource` and
  `centerPanelModel`. The controller compares the current tab, source and model with
  the ones it last announced and emits once when they differ: at the end of each
  current-tab announcement, after `currentSessionChanged`, and after the session's
  `selectNode()` in `selectCurrentNode()`. It emits nothing after shutdown; a change
  an observer makes from inside a notification announces itself, and the outer call
  then finds nothing to announce.
- `Main.qml`'s center `Loader` reloads on `centerPanelChanged`. Closing a tab before
  the current one changes none of the three, so the current view stays, the plot
  with its zoom. A tab switch rebuilds the view the session last chose; the table
  model keeps its channel, page, sample and slice.

## Text pages

- A page is 1,024 samples aligned on multiples of 1,024 (`textPageFirst(n) = n − n mod
  1024`), read with one `scanText(g, c, first, rows, control, visitor)` for the page's
  rows, its share of the stated samples, whose visitor always continues; the reader
  clamps the request to the channel's samples and delivers no more rows than the
  builder allocated. The rows are the samples the read delivered, so an unsorted
  group's short read shows what arrived.
- Row headers are absolute sample indices. The columns are Time and Text when the
  group's time axis is a master, the Time column named after it ("name [unit]", the
  plot's domain), and Text alone otherwise.
- A cell keeps at most 1,024 UTF-8 Bytes: a longer text keeps its longest prefix that
  ends on a character boundary and leaves room for "…", which marks the cut. The
  reader delivers an invalid sample as an empty text; it shows as an empty cell, and
  the footer counts the page's invalid samples (`ScanResult::invalidValues`).
- The sample field shows the page's first sample. Entering a sample shows the page
  holding it, clamped to the channel's samples; the arrows move one page. While a new
  page is read the previous rows are gone. Asking for the page shown or pending reads
  nothing, unless it failed or was refused.

## Array samples

- `mdf4array::arrayPlan()` plans the matrix on the GUI thread from the metadata and
  the source's axes only. Dimensions are outer first. With D ≥ 2 the rows are N(D−1),
  the columns N(D), and the slice holds one index per outer dimension, each clamped,
  a missing one 0; the element window is `{flat(slice) × rows × columns, rows ×
  columns}`, because the element index k is row-major outer first (`reader.h`
  `ElementWindow`). With D = 1 the array is N rows × 1 column.
- An axis labels its rows or columns with the zero-based index, with its fixed points
  (`axis_values`), or from its axis channel, itself a one-dimensional array channel
  read through `readArray`: its first N elements (`ca_type` 1), or its N + 1 borders
  as intervals (`ca_type` 3, left-open from its own `left_open`).
- The axis rule (ASAM MDF 4.2.0 BS p.118, p.128-129; `mdf4.proto` `ArrayDimension`)
  pairs array sample s with the axis channel's sample j, checked in this order: in the
  array's own group, j = s; in a one-cycle group without a time master, j = 0; when
  both groups have a time master, the axis sample with the latest time at or before
  sample s's, ties to the latest, found by scanning the axis group's master over its
  cycle count; otherwise the axis shows indices with the note "no common master with
  <axis>". With no axis sample at or before sample s it shows indices with "no <axis>
  sample at or before this one"; an axis read or master scan that fails otherwise
  shows indices with the reader's message. The values show regardless.
- `mdf4array::readArraySample()` reads the sample in the one task the session
  launches: `readArray(g, c, s, 1, window)` for the values and the sample's time,
  then per channel axis the master scan and `readArray(ag, ac, j, 1, {0, labels})`.
  Every read receives the task's `Control` unchanged, so each read's progress starts
  afresh and the table shows the highest so far. A Cancelled or SourceChanged outcome
  of any of them is the sample's.
- The corner names the two axes, "rows \ columns": an axis channel by its tree title,
  any other axis as the detail panel's Array card counts it ("Axis d"); a
  one-dimensional array names its rows alone. The header shows the sample's time with
  the time master's unit ("t = …"); without a master the time is the sample's index
  and is not shown.
- The sample field shows the sample: entering one shows it, clamped to the channel's
  samples; the arrows move one sample. Below them, one row holds a spin box per outer
  dimension of more than one element, numbered as the Array card counts the
  dimensions, in a strip that scrolls sideways and makes selectors as it shows them
  (its cache buffer included), not one per dimension, so the controls' height does
  not grow with the rank and the matrix keeps the pane;
  a singleton dimension has nothing to choose and no selector. A new sample or slice
  clears the rows while it is read. A column is as wide as its values and its header
  label; the row headers fit their labels and the corner's axis names, measured by a
  `TextMetrics` on the corner label's font so the measurement does not depend on the
  headers it sizes, up to 160 px.

## Reads and storage

- The table asks for a page, sample or slice through `ValueTableModel::requestWanted`,
  as the plot asks for exact samples through `detailWanted`; `wantedSample()` and
  `wantedSlice()` name it. The session installs a cached result at once, keeps the
  read in flight for the same page, or the same sample and element window, or cancels
  the read in flight and makes the new one its pending read. Selecting the channel
  asks for its first page, or its sample 0 with every slice index 0, the same way.
- A table read shares the session's single flight with the plot's scans, so the
  reader never runs two operations at once. Selecting another channel cancels it; its
  completion, and with it its result, is released before the next read starts.
- `TextPageBuilder::reservation(rows) = sizeof(TextPageBuilder) + sizeof(TextPage) +
  rows × (8 B time + 4 B end offset + 1,024 B cell cap)`, for the page's share of the
  stated samples, at most 1,024, is admitted against the session's result allowance
  before the read launches. The builder allocates all three arrays at construction,
  the text arena at rows × 1,024 B, and never reallocates or shrinks, so its live
  storage is its reservation (`sizeof(TextPageBuilder)` covers the `make_shared` block
  header) and every page of `rows`, delivered in full or short, holds `reservation(rows)
  − sizeof(TextPageBuilder)` as `TextPage::bytes()`, the structure and its capacities;
  the completion replaces the reservation with it in one step.
- `mdf4array::reservation(plan) = sizeof(ArraySample) + 8 B × (rows × columns + 1 +
  row labels + column labels)`, saturating, is admitted the same way. The plan refers
  to a fixed axis's points in the document (`AxisSource::fixed`, a pointer; the task
  keeps the metadata alive) and the read copies them once, into the result's labels.
  The reader reserves each series for its clamped request before it reads, and the
  read's vectors move into an `ArraySlice`, a smaller structure than `ArraySample` (a
  compile-time check in the session), so `ArraySlice::bytes()`, the structure and its
  value and label capacities, never exceeds the reservation; a note is not counted.
- `tst_tablestorage` counts both builders' live heap through the executable's own
  `operator new` against these reservations: a full page, a page of 700-Byte texts, a
  short delivery and a 65,536-element sample on a fixed axis of as many points.
- A page is retained like a plot result, keyed by its channel and first sample, an
  array slice by its channel, sample and element window: held while the table shows
  it, evicted least recently used when nothing holds it. Selecting the channel again
  shows its first page or sample from the cache. When held results leave no room the
  read is refused with "Texts need N of result memory, but results in use hold H of
  this file's A", or "Values need …" for an array.

## States and notes

`ValueTableModel::tableState` is NoChannel, Pending ("Reading texts…", "Reading
values…"), Shown, Empty, Failed or Refused. A text page with no rows reads "No samples
recorded", or "No samples: the recording ends before sample N". An array read that
succeeds without a sample, because the source holds fewer samples than it states and
the reader names no count, reads "No sample s: the recording ends after at most s
samples", or "No samples: the recording ends before its first one" for sample 0. A
failed or refused read says why in the plot's terms, its values named "Texts" or
"Values": "… could not be read: <reason> (<location>)", "… could not be read: the
file changed since it was opened; reopen it", "… need more working memory than the
decoder allows: …", "… exceed a reader limit: …" (an array slice above the reader's
`readValues` among them), or the allowance's refusal above. A shown array sample's
axis notes are the message. `busy` and `progress` describe the read in flight for the
selected channel. The footer shows the message, else a text page's "Rows a–b of N"
and its count of invalid samples.

The model's producer contract is `setTextChannel(header, page)` and
`setArrayChannel(header, slice)`, which show a channel with its first page or sample
(null while it is read), `setTextPage(page, note, text)`, `setArraySlice(slice, note,
text)`, `clear()`, `setBusy()` and `setProgress()`. Every change installs through one
reset that follows the detail model's pattern: content installed again from an
observer of the pre-reset notification replaces the content waiting there, and the
reset under way installs the newest. The model survives its own destruction from any
of its notifications, and the session stops after a table notification that
destroyed it or inside which a newer selection ran.

## Cell text

Values and axis labels show 7 significant digits, times 12; NaN shows as an empty
cell, infinities as "∞" and "−∞". An interval label reads `[a, b[`, or `]a, b]` when
left-open; an index label is the zero-based index.
