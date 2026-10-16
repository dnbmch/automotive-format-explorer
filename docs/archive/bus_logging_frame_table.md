# Bus-logging frame table

Locked plan. Explorer only, on the unchanged reader (mdf4-parser `75cced1`, its
contract in [reader.md](../../../mdf4-parser/docs/arch/reader.md) § Read set). The
value table's one-sample array view, the single-flight scheduler, the result
allowance, the 1,024-sample page and the 1 KiB cell cap stay as they are.

The backlog's BL-V5 fixed the shape: a frame table in place of the plot for a
bus-event structure, a row per frame with its time and each member's value, the
payload as hex, built like the value table from one request, the members nested
under the structure in the tree. Members keep their own views.

## Findings

1. **A Byte channel has no view.** `Mdf4DocumentSession::selectChannel` keeps the
   plot empty with "Byte channel — not plottable" for `Channel.binary`; the reader
   delivers its raw Bytes through `scanBytes` / `readBytes`, which the session's
   `Mdf4Source` does not bind (`scan`, `scanText`, `readArray`, `axis` only;
   `src/adapters/mdf4adapter.cpp` binds the same four).
2. **A structure's members are listed flat.** `buildTree()` appends every channel
   under its group row; `Channel.member_of` is not read anywhere in the Explorer.
   The ASAM corpus names members `CAN_DataFrame.ID` and so on, the parent's name,
   the group's `path_separator` (`.`, or 0 in `Vector_CAN_DataFrame_Sort_Bus.MF4`,
   whose names still carry the `.`) and the member's name.
3. **The text page is the frame table minus its columns.** `TextPage` holds a time
   and one UTF-8 text per row in a preallocated arena (`tabledata.h`); a frame page
   holds a time, a value or text per member and the structure's own Bytes per row.
   One page type with typed columns serves the text channel (one Text column), the
   Byte channel (one hex column) and the structure (member columns, then hex); the
   storage proof `tst_tablestorage` and `ValueTableModel`'s page kind cover all three.
   A second page type would repeat the builder, the model kind, the view's column
   rules and the storage test.
4. **The detail panel shows none of the bus-logging facts** the document carries:
   `member_of`, `bus_event`, `source_bus_type`, the group's `bus_event`,
   `plain_bus_event` and `path_separator` (`mdf4detailpresenter.cpp`,
   `channelDetails` / `groupDetails`).

## Design

### Page data (`src/models/tabledata.h`, `.cpp`)

- Renames, every reference updated: `kTextPageSamples` → `kPageSamples`,
  `kTextCellBytes` → `kCellBytes`, `textPageFirst()` → `pageFirst()`.
- `TableColumn { enum class Kind { Values, Text }; Kind kind; std::vector<double>
  values; std::vector<std::uint32_t> ends; std::vector<char> text; }` with
  `std::string_view textAt(row)` and `bytes()` (the structure and its array
  capacities). A Values column holds one double per row, NaN where invalid; a Text
  column the rows' UTF-8 texts one after another, as `TextPage` held them.
- `TablePage { firstSample; invalidValues; std::vector<double> time;
  std::vector<TableColumn> columns; rows(); bytes(); }` replaces `TextPage`;
  `TablePagePtr` replaces `TextPagePtr`. `invalidValues` counts the page's own
  lane's invalid samples, as the text page's did.
- `TableColumnSpec { TableColumn::Kind kind; std::size_t cellBytes = kCellBytes; }`:
  a Text column's cell cap in Bytes, its cut marker included.
- `TablePageBuilder(first, rows, const std::vector<TableColumnSpec>&)` allocates
  `time` for `rows`, and per column `values` for `rows` or `ends` for `rows` plus
  `text` for `rows × cellBytes`, at construction, and never reallocates.
  `static reservation(rows, columns)` = `sizeof(TablePageBuilder) + sizeof(TablePage)
  + columns.size() × sizeof(TableColumn) + rows × 8` + per column `rows × 8` (Values)
  or `rows × (4 + cellBytes)` (Text). For every page of `rows`, delivered in full or
  short, `page->bytes() + sizeof(TablePageBuilder) == reservation(rows, columns)`:
  `TablePage::bytes()` counts `columns.capacity() × sizeof(TableColumn)` and each
  column's capacities. Producer contract: `addTime(time, size)`,
  `addValues(column, values, size)`, `addText(column, texts, size)` (the text cut
  rule unchanged: the longest prefix within the cap that ends on a character boundary
  and leaves room for "…"), `addBytes(column, values, size)`, `finish(invalidValues)`.
- **Hex.** `addBytes` writes each value as two uppercase hex digits per Byte,
  Bytes separated by one space (`64 00 00 00`, the Explorer's hex is uppercase
  throughout); a value whose text would pass the cap keeps its first
  `(cellBytes − 3) / 3` Bytes and ends with "…"; a cap below the marker's three
  Bytes keeps nothing, the marker included. An empty value (invalid) is an empty
  cell.

### Page plan and read (`src/sessions/mdf4pageread.h`, `.cpp`, namespace `mdf4page`, Qt-free)

Mirrors `mdf4arrayread`: the plan on the GUI thread from the metadata, the read on
the worker.

- `ScanFunction`, `ScanTextFunction`, `ScanBytesFunction` in the reader's shapes.
- `Lane { enum class Kind { Values, Text, Bytes }; Kind kind; std::uint32_t
  channel; }`.
- `PagePlan { group; channel; Lane::Kind kind; first; rows; std::vector<Lane>
  members; const char* values; std::vector<TableColumnSpec> columns() const; }`.
  `kind` is the page's own lane: Text for a textual channel, Bytes for a Byte
  channel. `members` are the channel's **direct** members with a column, in document
  order: every channel of the group whose `member_of` is the channel's index and which
  is decodable and not an array: numeric → Values (through `scan`, a virtual member
  included), textual → Text, binary → Bytes (a member that is itself a structure
  contributes its raw Bytes; its own members are not columns here). An array member
  or a member that is not decodable has no column. `columns()` lists the members'
  specs in order, then the page's own column. `values` names the rows as the notes
  do: "Texts" for a text channel, "Frames" for a Byte channel with members, "Bytes"
  for one without.
- **Cell caps.** A Text member's cap is `kCellBytes`. A Bytes lane's cap is
  `3 × (bit_count / 8)` for a fixed-length (`cn_type` 0) or MLSD (5) Byte channel,
  whose value never exceeds its declared width, at most `kCellBytes`; a VLSD (1)
  Byte channel's cap is `kCellBytes`. The reader's own `Limits::payloadBytes`
  bounds a single value at 16 MiB before the cap applies.
- `pagePlan(document, group, channel, first)`: `rows` = the page's share of the
  stated count, `min(kPageSamples, sample_count − first)`, 0 past the end (the
  session's `pageRows` moves here).
- `reservation(plan)` = `TablePageBuilder::reservation(plan.rows, plan.columns())`.
- `PageResult { mdf4::ScanResult scan; TablePagePtr page; std::string note; }`.
- `readPage(plan, scan, scanText, scanBytes, control)`: the page's own lane first,
  `scanText` or `scanBytes` of `(group, channel, first, rows)`, every chunk's times
  into `addTime` and its texts or values into the own column; a status other than Ok
  returns that `scan` and no page. Then each member lane in order over the same
  `(first, rows)` through its function into its column. A member read that ends
  Cancelled or SourceChanged ends the page with that outcome and no page. One that
  ends Error or ResourceLimit leaves its column empty (the reader's chunks are
  provisional until a read returns Ok, `reader.h`) and appends
  "<member title> could not be read: <message>" (" (<location>)" when the outcome has
  one) to `note`, several joined with "  ·  ". Every read receives `control`
  unchanged, so progress restarts per lane and the table shows the highest so far,
  as the array sample's reads do. `scan` is the own lane's result; the page is
  `finish(scan.invalidValues)`. A member column shorter than the page's rows shows
  empty cells past its end (an unsorted group's short reads).
- The member title for the note is the tree's (`channelTitle` below); the plan
  carries it as `std::string name` on the `Lane`.

### Session (`src/sessions/mdf4documentsession.h`, `.cpp`)

- `Mdf4Source` gains `ScanBytesFunction scanBytes` as its fifth function;
  `Mdf4Adapter::load()` binds `reader->scanBytes`.
- `Request::Kind::TextPage` → `Page`; `Request` carries `mdf4page::PagePlan page`
  beside the array plan. `sameTableRead()` and `retainedTable()` keep their keys
  (channel, first; element window for an array).
- `tableRequest(key, sample, slice)`: an array channel as today; else
  `Page` with `mdf4page::pagePlan(*_metadata, g, c, pageFirst(sample))`.
- `selectChannel`: `!is_master && decodable && (textual || binary || has_array)`
  → `selectTable`; the "Byte channel — not plottable" branch and its message go.
- `selectTable`: `_table_model->setPageChannel(tableHeader(key), page)` for a page.
- `startPending`: a page's bytes are `mdf4page::reservation(request.page)`, its
  `what` the plan's `values`.
- `launch`: a page's task captures `scan`, `scanText`, `scanBytes`, the plan and
  the flag, runs `readPage`, and hands over `Completion{scan, page, note}`
  (`Completion` gains `std::string note`).
- `onScanFinished`: a page is retained by channel and first sample as the text page
  was; shown through `setPage(page, TableNote::None, note)`, or its failure through
  `noteFor(…, values)` as today.
- `tableHeader(key)`: `values` from the plan; `columns` from the plan's lanes, each
  `{channelTitle, kind == Values}`, then the own column `{"Text", false}` or
  `{"Bytes", false}`. The plan is the one place that decides the lanes, so the
  header and the read never disagree on column order.
- `channelRole`: a Byte channel with direct members → "Structure of N members"
  (N the count of channels whose `member_of` is its index); without → "Byte
  channel" as today. `treeBytes()` estimates a Byte channel's role at 36 B.
- A master-typed member is never its group's axis (MDF Bus Logging AS §4.2.4; the
  reader reads it as a value channel): `isAxis(channel)` = `is_master &&
  !has_member_of` decides the "Master channel" role, the Attribute kind and the
  not-plotted path, so such a member plots and is a value column.
- `mdf4page::channelTitle(group, index)`, the one title rule for the tree, the
  column headers, the member notes and an array's axis names: for a member, the
  group's `path_separator`, or "." when it states none; when the member's name
  starts with the parent's name followed by the separator and has more after it,
  that rest; else the name, else "Channel <index + 1>". The detail panel and the
  plot and table headers keep the full name.
- `buildTree()`: a channel with `member_of` is appended under its parent's row (the
  parent precedes it in the walk; keep the group's `TreeItem*` per channel index while
  building), titled by `channelTitle`, its subtitle, icon and semantic kind as any
  channel's. Row count, keys and paths are unchanged, so `treeBytes()`'s per-row cost
  holds.

### Model (`src/models/valuetablemodel.h`, `.cpp`)

- `enum Kind { Page, Array }`.
- `TableHeader` gains `QString values` (the notes' word for the rows; an array's is
  "Values") and `std::vector<TableColumnHeader> columns` with
  `TableColumnHeader { QString name; bool numeric; }` (Page).
- Page: `rowCount` the page's rows; `columnCount` `(timeName.isEmpty() ? 0 : 1) +
  columns.size()`; `data` the time cell for the time column, else `valueText` of a
  Values cell or the Text cell, an empty string past a column's end; horizontal
  `headerData` the time header as today, else `columns[c].name`; vertical the sample
  index. `Q_INVOKABLE bool numericColumn(int column) const`: a Values column, every
  array column; the time column is not. `Q_PROPERTY(bool hasTimeColumn …)`.
- Producer contract: `setPageChannel(header, page = {})`, `setPage(page, note =
  None, text = {})` replace the text pair; `page()` returns `TablePagePtr`.
- Messages take the header's word: Pending "Reading <values lowercased>…", Failed
  fallback "<values> could not be read", Refused fallback "<values> exceed the
  viewer's limits"; Shown joins the content's note and an array's axis notes as
  today. `cornerText` "Sample" for a page; `rangeText` and `invalidCount` for a page
  as for the text page; `hasNext` steps `kPageSamples` for a page.

### View (`qml/components/ValueTableView.qml`)

- Column width: an array column fits its content (as today); a page's time column
  `timeColumnWidth`; a page with one value column fills the rest with it (the text
  table as today); a page with more than one value column fits each like an array
  column (`Math.max(72, label + 12, header + 12)`).
- Alignment: `mapModel.numericColumn(column)` right, else left, in the header and
  cell delegates (an array stays right-aligned, a text column left).
- Nothing else changes: the sample field, the page arrows, the slice strip
  (array-only), the footer.

### Detail panel (`src/sessions/mdf4detailpresenter.cpp`)

- `busTypeText(quint32)`: 0 "None", 1 "Other", 2 "CAN", 3 "LIN", 4 "MOST",
  5 "FlexRay", 6 "K-Line", 7 "Ethernet", 8 "USB", 128 and above "Vendor-defined
  (N)", else "Reserved (N)" (BS p.81).
- Channel card: `Member Of` (the parent channel's name) when `member_of` is set,
  after Source Path. A "Bus" section after "Samples" when `bus_event` or
  `source_bus_type` is set: `Bus Event` (boolText), `Source Bus` (busTypeText).
- Group card: a "Bus" section after "Recording" when `bus_event`, `source_bus_type`
  or `path_separator` is set: `Bus Events` (boolText), `Plain Bus Events`
  (boolText), `Path Separator` (the text, "none" when empty), `Source Bus`.

## Tests

Every new case is run on the previous sources first and seen failing (or failing to
compile where it names new API); the implementer reports the failing-first counts.

- `tst_valuetablemodel`: the text cases on the page kind; `pageColumns` — a page of
  Time, a Values column, a Text column and the own column: cells, headers,
  `numericColumn`, `hasTimeColumn`, empty cells past a short member column, the
  Pending/Failed/Refused words from `values`.
- `tst_tablestorage`: the text cases on `TablePageBuilder`; `frame page: 1,024 rows,
  8 value columns, an 8-Byte MLSD hex column and a 15-Byte structure column` —
  `retained + sizeof(TablePageBuilder) == reservation`, peak within the reservation;
  a hex cut: a 400-Byte value under the 1 KiB cap keeps 340 Bytes and "…" (1,022
  Bytes of text), a 15-Byte value under its 45-Byte cap is 44 characters.
- `tst_mdf4documentsession`: the fake `Scans` gains a Bytes lane (`scanBytes`,
  value i of channel c = the Bytes `{c, i, 0x10 + i, 0x20 + i}` cut to `(i % 4) + 1`
  Bytes for an MLSD channel, all four otherwise), `openSession` binds it;
  `makeFrameDocument()`: group 0 adds, after the unsupported channel, `CAN_DataFrame`
  (binary, `cn_type` 0, `bit_count` 120), then its members with `member_of`:
  `CAN_DataFrame.ID` (numeric), `CAN_DataFrame.DataBytes` (binary, `cn_type` 5,
  `bit_count` 64), `CAN_DataFrame.Note` (textual), `CAN_DataFrame.Matrix` (array),
  `CAN_DataFrame.Odd` (not decodable), then a non-member signal; the group's
  `path_separator` ".". Cases:
  - `structureMembersNestUnderTheirParent`: the parent's subtitle "Structure of 5
    members"; its five children titled "ID", "DataBytes", "Note", "Matrix", "Odd"
    with their roles and kinds; the following signal back under the group.
  - `frameTableShown`: selecting the parent turns the center to the table: kind
    Page, role "Structure of 5 members", `values` "Frames", `hasTimeColumn`, columns
    Time, ID, DataBytes, Note, Bytes; the reads logged in order Bytes(parent),
    Values(ID), Bytes(DataBytes), Text(Note), each first 0 count 4, within one task
    (`maxConcurrent` 1); the cells of row 1 (time, ID's value, DataBytes "01 02"
    for i = 1 under the cut rule, Note "text 1", Bytes "05 01 11 21" for channel
    index 5 — adjust to the fixture's indices); "Rows 0–3 of 4"; the page held while
    shown and its `bytes()` the retained charge.
  - `byteChannelShowsItsBytes` replaces `byteChannelIsListedNotPlotted`: a Byte
    channel without members shows a page of Time and Bytes, role "Byte channel",
    `values` "Bytes", one `scanBytes` read.
  - `memberKeepsItsOwnView`: selecting ID plots it (an overview scan of its index);
    selecting DataBytes shows its own Bytes page.
  - `failedMemberLeavesItsColumnEmpty`: an Error outcome on the Note lane leaves the
    table Shown with empty Note cells and the message "Note could not be read:
    <message>"; a SourceChanged outcome on a member lane fails the page with "Frames
    could not be read: the file changed since it was opened; reopen it".
  - `pageReservationBecomesThePageCharge` on the frame document (the text case's
    shape); `closeCancelsTheTableRead_data` and `reentryFromTableNotifications_data`
    gain a frames row.
- `tst_mdf4tablecorpus`: `canFramesSortedByBus` — `BusLogging/CAN/
  Vector_CAN_DataFrame_Sort_Bus.MF4` group 0 channel 1: Shown, role "Structure of 9
  members", 11 columns headed `t [s]`, `BusChannel`, `DataBytes`, `DLC`, `Flags`,
  `ID`, `IDE`, `SingleWire`, `WakeUp`, `DataLength`, `Bytes`; row 0: `t`
  "0.019968196", BusChannel "1", DataBytes "64 00 00 00", DLC "4", ID "100",
  DataLength "4"; the Bytes cell 44 characters, starting "64 00 00 00" (the ID's
  four little-endian Bytes at the structure's start, `byte_offset` 8 of the record)
  and reading "04 04 64 00 00 00" from character 15 (DLC at 13, DataLength at 14,
  the payload from 15); "Rows 0–1,023 of 1,457"; `nextPage()` settled: row 432 (sample
  1,456) ID "18", DataBytes "00 01 00 00", `t` "7.960498196", "Rows 1,024–1,456 of
  1,457". The member values are mdf4-parser's `corpus_bus.cpp` expectations, an
  independent asammdf dump. `canFramesSortedById` — `Vector_CAN_DataFrame_Sort_ID.MF4`
  group 0 channel 1 (`CAN_DataFrame_100`, separator "."): members in the file's
  order (DataLength, WakeUp, SingleWire, IDE, ID, Flags, DLC, DataBytes, BusChannel),
  the virtual ID "100" and BusChannel "1" in rows 0 and 794, 795 rows on one
  page; the tree nests the nine members under `CAN_DataFrame` with the prefix
  stripped.
- `tst_valuetableview`: `showsContent_data` gains a frames page of five columns at
  300 and 700 px: no QML warning, time column 120 px, numeric columns right-aligned,
  text columns left.
- `tst_mdf4detailpresenter`: `channelCardNamesItsStructureAndBus` (Member Of, Bus
  Event, Source Bus "CAN"), `groupCardNamesBusEvents` (Bus Events, Plain Bus Events,
  Path Separator ".", Source Bus); `byteChannelIsReadableNotPlottable` keeps its
  Plottable "No" and Values "Raw Bytes".

## Docs

- [value_table.md](../ref/value_table.md): the text-page section becomes "Pages"
  (columns, the hex rule and caps, the member lanes and their failure rule, the
  reservation formula, the storage proof); the center switch names Byte channels;
  the states take the `values` word.
- [architecture.md](../arch/architecture.md) § MDF4 reads: `Mdf4Source` of five
  functions; a page read as one task; the tree nests a structure's members.
- [README.md](../../README.md): Byte channels and bus-logging structures open the
  table; members nest under their structure.
- [backlog.md](../backlog.md): BL-V5 removed; BL-V6 "frame payload decoded against
  the attached database" with its trigger (the reader's attachment references, its
  backlog).
- [mdf4_viewer.md](mdf4_viewer.md): the foreign-recording acceptance names bus logs.
- Workspace: `BACKLOG.md` § MDF4 data viewer, `docs/handoff.md`,
  `docs/ref/orchestrator_prompt.md`'s state line. This plan is archived with the
  harvest.

## Proof of completion

- Every Explorer suite green with 0 compiler warnings on a clean rebuild of
  `build-tables-explorer/` (SOURCE, Debug); the corpus suites' new cases pass against
  the local corpus; the failing-first counts reported per new case.
- The lead reads every diff; an independent audit re-derives the storage proof, the
  lane rules and the corpus expectations; srv-one passes the pushed heads (the corpus
  skips there).
- Operator-visual, recorded in the handoff: the CAN frame table and the nested
  members on `Vector_CAN_DataFrame_Sort_Bus.MF4`.

## Out of scope

- A structure parent that is not decodable (not a fixed-length Byte array) stays
  listed as not plottable; its members still nest under it.
- A nested structure's members as columns of the outer table.
- Decoding the payload against the attached database (BL-V6; the reader does not
  read `cn_at_reference` yet).
- The reader. A page's lanes each traverse the page's records once, an unsorted
  group's prefix included: the reader's cost model, unchanged.
