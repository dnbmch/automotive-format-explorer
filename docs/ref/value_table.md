# Value table

The value table is the MDF4 session's center view for a channel whose samples are
not a signal to plot. A decodable text channel (`Channel.textual`) or Byte channel
(`Channel.binary`) shows its samples in pages, one row per sample with its index,
time and value; a Byte channel that is a compact structure's parent, such as a bus
log's `CAN_DataFrame`, adds a column per direct member. A decodable array channel
(`Channel.array`) shows one sample at a time as a matrix with its axes as headers.
Pages read through the reader's value, text and Byte lanes, array samples through
its array lane, on the session's single-flight scheduler and within the session's
result allowance ([MDF4 reads](../arch/architecture.md#mdf4-reads)).

`src/models/tabledata.h` holds the format-neutral table data (`TablePage` with its
`TableColumn`s, `TablePageBuilder`, `ArraySlice`); `ValueTableModel`
(`src/models/valuetablemodel.h`, explorer-core) is the view's `QAbstractTableModel`;
`qml/components/ValueTableView.qml` is the view. `src/sessions/mdf4pageread.h`
(namespace `mdf4page`) plans and reads one page, `src/sessions/mdf4arrayread.h`
(namespace `mdf4array`) one array sample; both are Qt-free.

## Center switch

- `Mdf4DocumentSession` keeps its center: the plot, or the table while a decodable
  text, Byte or array channel is selected. `centerPanelSource()` and
  `centerPanelModel()` follow it: `SignalPlotView.qml` with the `SignalPlotModel`, or
  `ValueTableView.qml` with the `ValueTableModel`. Selecting anything else returns the
  center to the plot. The view that hands over is cleared, so it holds no result.
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

## Pages

- A page is 1,024 samples aligned on multiples of 1,024 (`pageFirst(n) = n − n mod
  1024`). `mdf4page::pagePlan()` plans it on the GUI thread from the metadata: its
  rows, the page's share of the stated samples; its own lane, the channel's texts
  through `scanText` or its Bytes through `scanBytes`; and a member lane per direct
  member with a column, in document order. A direct member is a channel of the group
  whose `member_of` is the channel's index; it has a column when it is decodable and
  no array: a numeric one, a virtual one included, through `scan`, a textual one
  through `scanText`, a Byte channel through `scanBytes`. A member that is itself a
  structure contributes its own Bytes; its members are not columns of the outer page.
- Row headers are absolute sample indices. The columns are the time when the group's
  time axis is a master, named after it ("name [unit]", the plot's domain), then a
  column per member lane, titled as the tree titles the member, then the channel's
  own, "Text" or "Bytes". The table header's columns come from the same plan as the
  read, so the two agree on the order. The rows' word in the notes is "Texts" for a
  text channel, "Frames" for a Byte channel with member lanes and "Bytes" for one
  without (`TableHeader::values`).
- `mdf4page::readPage()` reads the page in the one task the session launches. The
  own lane comes first, `(g, c, first, rows)`, every chunk's times into the time
  column and its texts or Bytes into the own column; a status other than Ok ends the
  page with that outcome and no page. Then each member lane reads the same samples
  into its column. A member read that ends Cancelled or SourceChanged ends the page
  with that outcome; one that ends Error or ResourceLimit leaves its column empty
  (the reader's chunks are provisional until a read ends Ok) and adds "<member> could
  not be read: <reason> (<location>)" to the page's note (`TablePage::note`), several
  joined with "  ·  ", which the footer shows in place of the rows' range. Every
  read receives the task's `Control` unchanged, so each read's progress starts afresh
  and the table shows the highest so far. The rows are the samples the own lane
  delivered, so an unsorted group's short read shows what arrived; a column shorter
  than the page's rows shows empty cells past its end.
- A value cell shows the number as an array's cell does. A text cell keeps at most
  1,024 UTF-8 Bytes: a longer text keeps its longest prefix that ends on a character
  boundary and leaves room for "…", which marks the cut. A Byte cell is hex, two
  uppercase digits per Byte separated by one space (`64 00 00 00`); a value whose text
  would pass the cell's cap keeps its first (cap − 3) / 3 Bytes and ends with "…".
  The cap of a fixed-length (`cn_type` 0) or MLSD (5) Byte channel is three
  characters per declared Byte (`3 × (bit_count / 8)`), at most 1,024, since its value
  never exceeds its width; a VLSD (1) Byte channel's is 1,024. The reader's own
  `Limits::payloadBytes` bounds a single value at 16 MiB before the cap applies.
- The reader delivers an invalid sample as NaN, an empty text or an empty Byte value;
  each shows as an empty cell, and the footer counts the invalid samples of the page's
  own lane (`ScanResult::invalidValues`).
- Numbers are right-aligned, the time and texts left (`ValueTableModel::numericColumn`).
  The time column is 120 px wide; a page with one value column gives it the rest of
  the pane, a page with more fits each column to its values and its header label.
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
  reader never runs two operations at once; a page's lanes and an array sample's
  axis reads run one after another in its one task. Selecting another channel cancels
  it; its completion, and with it its result, is released before the next read starts.
- `TablePageBuilder::reservation(rows, columns) = sizeof(TablePageBuilder) +
  sizeof(TablePage) + columns × sizeof(TableColumn) + rows × 8 B time`, plus per
  column `rows × 8 B` for values or `rows × (4 B end offset + its cap)` for text, for
  the page's share of the stated samples, at most 1,024, is admitted against the
  session's result allowance before the read launches (`mdf4page::reservation(plan)`,
  the plan's columns). The builder allocates every array at construction, a text
  column's arena at rows × its cap, and never reallocates or shrinks, so its live
  storage is its reservation (`sizeof(TablePageBuilder)` covers the `make_shared`
  block header) and every page of `rows`, delivered in full or short, holds
  `reservation(rows, columns) − sizeof(TablePageBuilder)` as `TablePage::bytes()`, the
  structure, its column slots and their capacities; its note is not counted. The
  completion replaces the reservation with it in one step.
- `mdf4array::reservation(plan) = sizeof(ArraySample) + 8 B × (rows × columns + 1 +
  row labels + column labels)`, saturating, is admitted the same way. The plan refers
  to a fixed axis's points in the document (`AxisSource::fixed`, a pointer; the task
  keeps the metadata alive) and the read copies them once, into the result's labels.
  The reader reserves each series for its clamped request before it reads, and the
  read's vectors move into an `ArraySlice`, a smaller structure than `ArraySample` (a
  compile-time check in the session), so `ArraySlice::bytes()`, the structure and its
  value and label capacities, never exceeds the reservation; a note is not counted.
- `tst_tablestorage` counts both builders' live heap through the executable's own
  `operator new` against these reservations: a full text page, a page of 700-Byte
  texts, a short delivery, a frame page of 1,024 rows with eight value columns, an
  8-Byte MLSD hex column and a 15-Byte structure column, and a 65,536-element sample
  on a fixed axis of as many points; it also checks the hex cut.
- A page is retained like a plot result, keyed by its channel and first sample, an
  array slice by its channel, sample and element window: held while the table shows
  it, evicted least recently used when nothing holds it. Selecting the channel again
  shows its first page or sample from the cache. A page holds its note and an array
  slice its axis notes, so a result from the cache shows the message its read showed.
  When held results leave no room the read is refused with "<rows> need N of result
  memory, but results in use hold H of this file's A", the rows named "Texts",
  "Frames" or "Bytes" for a page and "Values" for an array.

## States and notes

`ValueTableModel::tableState` is NoChannel, Pending ("Reading texts…", "Reading
frames…", "Reading bytes…", "Reading values…"), Shown, Empty, Failed or Refused. A
page with no rows reads "No samples recorded", or "No samples: the recording ends
before sample N". An array read that succeeds without a sample, because the source
holds fewer samples than it states and the reader names no count, reads "No sample
s: the recording ends after at most s samples", or "No samples: the recording ends
before its first one" for sample 0. A failed or refused read says why in the plot's
terms, its rows named by the header's word, "Texts", "Frames", "Bytes" or "Values":
"… could not be read: <reason> (<location>)", "… could not be read: the file changed
since it was opened; reopen it", "… need more working memory than the decoder
allows: …", "… exceed a reader limit: …" (an array slice above the reader's
`readValues` among them), or the allowance's refusal above. A shown page's note and a
shown array sample's axis notes are the message. `busy` and `progress`
describe the read in flight for the selected channel. The footer shows the message,
else a page's "Rows a–b of N" and its count of invalid samples.

The model's producer contract is `setPageChannel(header, page)` and
`setArrayChannel(header, slice)`, which show a channel with its first page or sample
(null while it is read), `setPage(page, note, text)` and `setArraySlice(slice, note,
text)`, whose note and text say why no page or slice shows, `clear()`, `setBusy()`
and `setProgress()`. The header (`TableHeader`) carries
the rows' word (`values`) and a page's columns after its time column, each a name
and whether it holds numbers. Every change installs through one reset that follows
the detail model's pattern: content installed again from an observer of the
pre-reset notification replaces the content waiting there, and the reset under way
installs the newest. The model survives its own destruction from any of its
notifications, and the session stops after a table notification that destroyed it
or inside which a newer selection ran.

## Cell text

Values and axis labels show 7 significant digits, times 12; NaN shows as an empty
cell, infinities as "∞" and "−∞". An interval label reads `[a, b[`, or `]a, b]` when
left-open; an index label is the zero-based index. A Byte cell is uppercase hex
(Pages above).
