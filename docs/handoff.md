# automotive-format-explorer — handoff

## 2026-10-17 — Byte channels and bus-logging structures show pages; members nest — OPEN

Selecting a decodable Byte channel turns the center to the value table, a page of
its samples' times and raw Bytes as hex; a compact structure's parent, such as a
CAN log's `CAN_DataFrame`, adds a column per direct member (a number, a text or hex)
before its own Bytes, read lane by lane in one task over the page's rows
([value table](ref/value_table.md#pages)). The text page is the one-column case of
that page (`TablePage`, `TablePageBuilder`, `mdf4page`). A structure's members are
rows under its row, titled by the rest of their name after the parent's name and
the group's separator; the detail panel keeps the full name and gains the
bus-logging cards (Member Of, Bus Event, Source Bus; the group's Bus Events, Plain
Bus Events, Path Separator, Source Bus). A master-typed member is a value channel,
never the axis. The reader is unchanged (mdf4-parser `75cced1`); the plan is
archived: [bus-logging frame table](archive/bus_logging_frame_table.md). OPEN only
for the check below.

Evidence: 26 of 26 suites green (ctest exit 0) in the workspace's
`build-tables-explorer/` (SOURCE, Debug), 0 compiler warnings on a clean rebuild;
the corpus suites ran their cases against the local ASAM examples, the new ones
over `BusLogging/CAN`, every cell tied to mdf4-parser's `corpus_bus.cpp`
expectations (an independent asammdf dump) and to a raw-record decode of the
files. Every new case was seen failing first: the implementer's (tree nesting 12
rows against 7, the table not opening, compile errors on the old types) and the
lead's three (the emptied member column showing "text 0", the master-typed member
labelled "Master channel", the cell-cap pin reading 1,024 against 45 under a
widened cap). `tst_tablestorage`: the frame page of eight value columns, a 24-Byte
MLSD hex column and a 45-Byte structure column reserves 153,568 B and retains
153,552, peak equal to the reservation, with and without an emptied column; the
text page keeps 1,061,064 / 1,061,048. An independent xhigh audit confirmed every
clause by its own derivation and its own parse of the CAN files, found no defect
and six low findings, all fixed in the batch: the cap rule pinned apart from the
plan, a storage case for the emptied column, two docs still naming text and array
channels only, a master-typed member shown as the axis, the hex cut's arithmetic
below a three-Byte cap; a document whose member precedes its parent would null
the tree parent, which the reader's indexing invariant excludes.

The independent review of the frame table (the workspace's
`build-frame-independent-review/REVIEW.md`) found one P2 defect, corrected: a page
whose failed member read emptied a column lost its note when the cache installed it
again, on selecting the channel again or returning to the page, so the footer showed
the plain row range over the empty column. The note is the page's own
(`TablePage::note`, given to `TablePageBuilder::finish`), so the cache, the table and
the read hold it with the cells; the model reads a shown page's note from the page as
it reads an array slice's axis notes, and a producer's text says only why nothing
shows. The note's string slot is part of the page's structure and its reservation;
its text is not counted. `tst_mdf4documentsession`'s `cachedPageKeepsItsNote`, both
rows, failed on `11b5435` at the note after confirming the cache hit and the empty
column, and passes; the seven table suites are green, 0 warnings. The independent
reviewer accepted the correction with no new finding: its own session probe and both
rows pass, each cache route showing the note with no new read and the column empty,
the next page its text and no note.

UNVERIFIED — operator-visual, the CAN frame table: open the corpus's
`BusLogging/CAN/Vector_CAN_DataFrame_Sort_Bus.MF4`. Pass: the group lists `t` and
`CAN_DataFrame` ("Structure of 9 members") with nine rows under it titled
`BusChannel` … `DataLength`, no `CAN_DataFrame.` prefix; selecting `CAN_DataFrame`
shows eleven columns, `t [s]` at 120 px, the numbers right-aligned, `DataBytes` and
`Bytes` left; row 0 reads 0.019968196, 1, "64 00 00 00", 4, 0, 100, 0, 0, 0, 4 and
"64 00 00 00 00 04 04 64 00 00 00 00 00 00 00", the footer "Rows 0–1,023 of 1,457",
the next-page arrow "Rows 1,024–1,456 of 1,457" with ID 18 and "00 01 00 00" in its
last row; a member's detail panel shows Member Of and a Bus card (Bus Event Yes,
Source Bus CAN), the group's Bus card Path Separator "none"; `ID` plots and
`DataBytes` shows its own hex page. Fail: a flat member list, a plot or plot note
for `CAN_DataFrame`, clipped or misaligned columns, or other cell values.

## 2026-10-13 — text and array channels show value tables — OPEN

Selecting a decodable text channel switches the center panel from the plot to a
value table of its samples, one page of 1,024 per read through the reader's text
lane; selecting a decodable array channel shows one sample as a matrix with its
axes as headers, read through the array lane with the spec's common-master pairing
of axis samples ([value table](ref/value_table.md), [center panel and
scheduling](arch/architecture.md)). The controller announces the switch as
`centerPanelChanged`; table results are admitted, retained and evicted within the
session's result allowance like plot results, and a reservation above the whole
allowance is refused outright. Evidence: the session, controller, model and view
suites with their new cases seen failing first; the array unit and the corpus suite
against the 4.2.0 examples (POR, VMA, dSPACE, VLSD, unsorted VLSD); every Explorer
suite green with 0 warnings against mdf4-parser `ff1483f`. OPEN only for the checks
below.

The independent review of the landed tables found two P2 defects, corrected in
`5fb890a` and `f2f2fbf` (plan archived: [table storage and slice
controls](archive/table_storage_and_slice_controls.md)): a table read's builders held
more live heap than the reservation the session admitted (the text arena's doubling
and shrink, three copies of a fixed axis), and the slice controls took the whole
viewport at a high rank (126 spin boxes, a 0 px matrix for 128 singleton dimensions).
Now the text page builder allocates its storage at construction and never reallocates,
a fixed axis is a pointer into the document copied once into the result, the selectors sit in
one lazy strip for the outer dimensions with a choice, and the row headers measure the
corner text with `TextMetrics`, which also removed a binding loop that fired whenever
the view opened before its first sample arrived (the app's cache-miss path). Evidence:
`tst_tablestorage` (heap counted by the executable's own `operator new`) and the view
suite's three new cases seen failing first; the reviewer's probes rebuilt on the fixed
sources read peak 1,060,968 of 1,060,968 (text) and 1,048,584 of 1,048,856 (array)
against 1,585,256 and 2,097,160 before; 26 of 26 suites green with 0 warnings; the
reviewer accepted both corrections with no new defect found.

UNVERIFIED — operator-visual, the slice strip: open the corpus's
`Arrays/Simple/dSPACE_MeasurementArrays.mf4`, select `Signal_MatrixDim` (2 × 3 × 4).
Pass: one row under the sample controls reading "Slice", "1" and a spin box 0–1, the
3 × 4 matrix filling the rest of the pane, the spin box stepping the matrix to the
other slice; a 2-D array (KF4 below) shows no such row. Fail: a wrapped or missing
row, a spin box for a singleton dimension, or a matrix squeezed by the controls.

UNVERIFIED — operator-visual, three opens from the corpus's
`ASAM_MDF_BS_V4-2-0/BaseStandard/Examples/`:
- `ChannelTypes/VLSD/Vector_VLSDStringUTF8.mf4`, select the first group's `Data
  channel`. Pass: the center turns into a table with the chip "Text channel", rows
  0–9 reading "zero" through "nine" at times 0–9, the footer "Rows 0–9 of 10";
  selecting a numeric channel brings the plot back; switching tabs and returning
  shows the same table.
- `Arrays/Classification/Porsche_2D_classification_result.mf4`, select
  `2D_classification`. Pass: chips "s" and "Classification result 52 × 142", 52 rows
  × 142 columns, the corner "throttle \ nmot", the first row and column headers
  starting "[−∞, " and the last ending ", ∞[", cell (1, 0) "3698.8" and (26, 0)
  "10697.06", no footer note.
- `Arrays/Simple/Vector_MeasurementArrays.mf4`, select `KF4` (ninth group). Pass:
  chip "Look-up 6 × 8", the corner "Axis 1 \ Curve1", rows headed 0…5 and columns
  0…7, the footer "no Curve1 sample at or before this one", cell (2, 4) "24", the
  header "t = 0.660880207" with the master's unit.
- Fail, any of them: a plot or plot note, an empty table, a stuck busy indicator,
  index headers on POR, cut-off headers, other values in those cells, a missing note,
  or a crash.

## 2026-10-11 — invalidated samples plot as gaps — OPEN

The MDF4 reader delivers a sample whose invalidation bit is set as NaN at its time
([reader contract](../../mdf4-parser/docs/arch/reader.md#read-set)); the plot draws a
nonfinite value as a gap and the detail panel's Plot section states the channel's
validity rule (`8061dd0`). OPEN only for the check below.

UNVERIFIED — operator-visual: author a file with asammdf 8.8.18 (`MDF(version="4.10")`,
`append([Signal(samples, timestamps, name, invalidation_bits=mask)])`, 40 samples at
10 ms, the mask set at indices 0, 3, 4, 17 and 39), open it and select that channel.
Pass: the exact-sample line breaks at t = 0.00, 0.03, 0.04, 0.17 and 0.39 s, the
hover over a gap names no value, and the detail panel reads `Invalidation bit 0
(Byte 0, bit 0 of the invalidation Bytes)`. Fail: a continuous line through those
times, or a value shown there.

## 2026-10-06 — MDF4 detail labels and opening cancellation — OPEN

Landed: the MDF4 detail panel labels the big-endian data types, the algebraic and
value-range conversion kinds and the formula text; the load contract carries a
cancellation flag that `AppController::shutdown()` sets, and the MDF4 reader observes
it while opening ([adapter contract](arch/adapter_contract.md),
[opening and shutdown](arch/architecture.md#opening-files-and-shutdown)). The text
parsers cannot stop, so shutdown still waits for an A2L, DBC or LDF parse. Every
suite is green locally. OPEN only for the check below.

UNVERIFIED — closing during a large MDF4 opening: open a large `.mf4` and close the
window while the loading indicator shows. Pass: the window closes almost at once.
Fail: it closes only after the opening finishes.

## 2026-09-28 — tab ownership and bounded recording viewer — OPEN

`master` needs mdf4-parser `c98c298` and its public lib `933439f` (the bounded
reader: scan, typed outcomes, limits). All three are pushed and carried by the
[latest Explorer release](https://github.com/dnbmch/automotive-format-explorer/releases/latest).

Contracts: [tabs](arch/architecture.md#tabs), [tree navigation](arch/architecture.md#tree-navigation),
[node keys](arch/architecture.md#node-keys), [MDF4 reads](arch/architecture.md#mdf4-reads),
[adapter contract](arch/adapter_contract.md), [signal plot](ref/signal_plot.md),
[memory view](ref/memory_view.md). Remaining MDF4 scope, including real-recording
acceptance: [plan](plans/mdf4_viewer.md).

Landmines:
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
  runtime's tools first ([runtime provenance](ref/cmake_build_system.md#runtime-provenance-on-windows)).

UNVERIFIED — the 16 GB reference laptop: build `build-i2i3/b/measure/` (its
`CMakeLists.txt`) against the landed reader and Explorer and run its scenarios
(`run_measurements.py`) there with the generated 14-hour recordings; the recorded
figures come from a 64 GB host whose page cache held the 10 GB file, and from builds
before the reader's stream-trailer check. Pass = the same order of overview and window
times on a cold first read, cancellation well under 100 ms, peak commit about 75 MiB
with a full window; fail = a peak that grows with the recording, or a close or
selection change that waits for a scan to finish.

UNVERIFIED — operator-visual, one Windows launch of a fresh build, with the four
samples and a large recording (the generated 14-hour files or a real one):
- File > Open lists "Automotive files (*.a2l *.dbc *.ldf *.mf4)", then A2L, DBC, LDF,
  MDF4 and All files, each narrowing the listing. The empty sidebar's four sample
  links open; long tab names elide with "…".
- In each sample, expand categories and entities, select a row and scroll; switch
  between the tabs, also in quick succession: each comes back as left.
- Type a filter, switch away and back: the filter text and filtered tree come back;
  clearing the filter restores the tree from before it, its selection included, also
  when nothing was selected or the selected row sat in a collapsed category.
- Close the current middle tab: its right neighbour is highlighted and shown. Close a
  tab left of the current one: the current plot keeps its zoom, a memory view its
  scroll and selection.
- Show the raw view on an A2L module row, then select other rows and an MDF4 channel:
  the JSON follows the selection. LDF's overview row offers no raw view.
- Closing the window during a large A2L load exits after the parse.
- An A2L whose objects span more than 16 MiB, for example RAM and flash without
  `MEMORY_SEGMENT`s: objects at both ends of the derived segment paint in their
  colors; scrolling or "Go to" reaches a far address and flash-highlights its object;
  clicking an object selects it in the tree; overlaps far into the segment are hatched.
- `demo_recording.mf4`: `speed` plots the sine with zoom, pan, hover and reset; `t`
  shows the master-channel notice. A foreign `.mf4`: numeric channels plot, exotic
  ones say "not plottable".
- Large recording: selecting a channel shows "Reading samples…" with a percentage,
  then the whole recording as vertical extrema columns with no line joining them; the
  header reads "Overview" and the sample count. A channel that fits one window turns
  to "Exact samples" shortly after.
- Hovering the overview highlights a band and reports a time range, an index range,
  the sample count and min/max; hovering exact samples shows a crosshair with the
  value and "index N".
- Zooming in shows a line through the exact samples, with gaps where values are
  missing; zooming out past about four million samples returns to the overview with
  "zoom in for exact samples". Reset view returns to the whole overview at once.
- Clicking several channels quickly, or zooming repeatedly, settles on the last one
  without showing an intermediate result; closing the tab during a long overview is
  prompt.
- A file whose source ends early shows "Incomplete: N of M samples", and still
  "Incomplete" with the center pane dragged to its narrowest; truncating or appending
  to an open file makes the next channel read "…the file changed since it was opened;
  reopen it".

Fail = a missing or unfiltered dialog entry, a dead sample link, clipped tabs; a tab
showing another tab's expansion or scroll, a lost filter or pre-filter tree, a
highlighted tab other than the one shown, a rebuilt plot, an empty or stale raw view;
an unoccupied cell where an object belongs, a scrollbar that cannot reach the far end,
a click that selects nothing; an empty `speed` plot or an empty plot without
explanation, a line joining overview columns, a hover naming a single sample over the
overview, a stale channel or view shown last, a short file presented as complete, a
stuck progress indicator, a close that hangs; a crash.
