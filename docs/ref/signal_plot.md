# Signal plot

The signal plot is the format-neutral center view for sampled numeric data.
MDF4 is its first producer; producer-specific parsing, metadata, and sample
types stop in the document session. `SignalPlotItem` is a `QQuickPaintedItem` like
the grids, so the plot adds no Qt module and no deploy surface.

The plot stack (`plotdata`, `SignalPlotModel`, `SignalPlotItem`, `SignalPlotView.qml`)
takes operator-authored changes only, so it can be relicensed for a live view in the
proprietary product apps.

## Data seam

`src/models/plotdata.h` holds the plot's data. A producer's worker builds it from
the chunks of one scan, `(firstSample, time, value, size)` in sample order with
absolute sample indices; the plot installs it immutable and shared, so a producer's
cache and the plot hold one copy.

- **Overview** (`PlotOverview`, `PlotOverviewBuilder`): a whole signal in at most
  4,096 bins. Each bin is a run of consecutive samples inside one aligned run of
  2^`binShift` indices, with its first sample, sample count, finite-value count,
  first and last coordinate, widest gap between consecutive coordinates and finite
  extrema (NaN when no value is finite). Bins cover exactly the delivered samples,
  so none is empty. The builder allocates `clamp(stated count, 2, 4096)` bins at
  construction and never again: when the scan outgrows them, neighbors merge
  pairwise into bins twice as wide. A count the source overstates, a short scan
  among them, therefore costs no resolution, and the index arithmetic holds for
  every index a 64-bit sample count reaches. A fixed bin count keeps an overview's
  size independent of the recording's length.
- **Domain** (`PlotDomain`): one per result set, decided over the whole overview
  scan, chunk seams included. The producer's coordinates are used only when every
  one is finite and nondecreasing; otherwise the overview and every window of its
  result set plot against absolute sample indices, which the bins already
  partition. A source that ends early keeps the bounds and count it delivered and
  reports `incomplete()`; nothing stands for the missing tail.
- **Exact window** (`PlotWindow`, `PlotWindowBuilder`): consecutive samples with
  their coordinates and values and per-256-sample extrema, at most 4 Mi samples
  (64 MiB of doubles) including one neighbor each side: the reader's own `read()`
  cap, and over a thousand samples per pixel column of a 4K-wide plot. `PlotOverview::windowRequest()`
  turns a domain range into the conservative index cover of the bins that may hold
  it, widened by one sample each side; the builder scans that cover, keeps the
  samples in range and the two neighbors by their actual coordinates, and refuses
  when they exceed the limit. A window holds every sample whose coordinate lies
  strictly between its first and last one, or up to the edge of the data where it
  reaches it (`covers()`). Samples that contradict their overview (a coordinate
  going back, or a cover that no longer brackets the range) fail the window.
- **Storage.** `PlotOverviewBuilder::reservation()` and
  `PlotWindowBuilder::reservation()` give the most a builder and its result hold
  before either exists; a result's `bytes()` is its structure and array capacities.
  An overview retains its reservation less the builder object; a window allocates
  its arrays once, at its first kept sample, for what its cover can still deliver.

The model's producer contract is `setSignal(header, overview, note, text)`, which
starts a new result set and shows the whole overview, `setWindow(window, note,
text)`, which keeps bounds and zoom, `clear()`, `setBusy()` and `setProgress()`.
`PlotHeader` carries the signal and domain names and units; an Index result set shows
"Sample index". `PlotNote` says why nothing, or only the overview, is shown: Empty
(with the producer's reason, or "No samples recorded"), Failed, or Refused.

## States

`SignalPlotModel::plotState` is NoSignal, Pending (the overview is being read),
Empty, Failed, Refused, Overview (each column spans the extrema of the samples it
covers) or Detail (exact samples cover the whole view). `message` explains the
state: the producer's note, or in Overview why the view is not exact ("More than
4,194,304 samples in view; zoom in for exact samples", "Reading exact samples…").
`incomplete` and `countText` ("N of M samples") mark a source that ended early.
`busy` and `progress` (0 to 1, negative when unknown) describe the producer's work.

The view asks for detail itself: after a new signal or a moved view that the
installed window does not cover, the model emits `detailWanted`, and
`detailRequest()` names the window it wants: the view and as much again each side
when that fits one window, else the view alone, else none. The model holds a window
only while it covers the view and drops it as the view leaves; resetting the view
returns to the overview at once.

## Rendering

Every paint is sized by the viewport or the overview's bins:

- in Overview, `columns()` gives each pixel column the extrema of the bins that reach
  it; a bin fills the columns between its first and last sample unless a gap inside
  it is wider than a column. Bins are never joined, so no line suggests that one
  bin's extremum follows another's;
- in Detail, while the visible samples are at most two per pixel, `SignalPlotItem`
  draws the line through them, one sample past each edge; a nonfinite value breaks
  the line and a sample alone between breaks is a dot. Denser, each pixel column
  spans the extrema of its samples, from the window's block summaries;
- axes and tick labels are painted in the same item, avoiding a QML object per
  sample or tick.

The zoom floor is the smallest positive spacing the overview found, never below what
doubles resolve at the axis' magnitude, so irregular recordings can still zoom into
dense bursts separated by large gaps.

## Interaction

- Mouse wheel zooms the domain axis around the pointer.
- Left-button drag pans the visible domain window.
- Hover over exact samples snaps to the nearest one and reports its coordinate,
  actual value and absolute sample index. Hover over the overview reports the bins
  under the pointer's pixel column: their coordinate range, index range, sample
  count, nonfinite count and extrema, and marks their band, never a single point.
- Reset view (the header button or a double-click) restores the full domain range
  and recomputes the visible value range.

The value axis follows the finite extrema in view, with padding for readability:
of the exact samples in Detail, of the bins the view touches in Overview.

The header shows the signal, its unit, the sample count where it fits, whether the
view is the overview or exact samples, and progress. A short source is marked at
every width the center pane allows: "Incomplete: N of M samples" where that fits,
"Incomplete" below it. The footer shows the hover report, or the view's range with
the state's message.

## MDF4 selection lifecycle

Opening an MDF4 file indexes only its metadata graph, once, into the session's
`mdf4::Reader`. Selecting a plottable channel scans its whole range into an
overview on a worker, then the plot's detail requests scan windows. Scheduling,
cancellation, the 256 MiB result allowance, outcomes and teardown:
[architecture](../arch/architecture.md#mdf4-reads).

A group's master channel carries the domain rather than a signal against it —
decoding a master returns its own samples in both time and value — so the tree
lists it as the group's axis channel, with its detail view intact, and never
scans it. A channel the reader cannot decode stays in the tree marked "Not
plottable", with its reason in the detail cards; selecting it scans nothing and the
plot says it is not plottable.

## Measured scale

Measured on the development host: i9-12950HX, processes pinned to its eight
performance cores, 64 GB, Samsung MZVL22T0HBLB NVMe, Windows 11 Pro 26200; Release
(`-O3`) Explorer libraries and reader; files read from a warm page cache. The files are
the reader's generated 14-hour 10 kS/s recordings of 504,000,000 samples per channel,
an unsorted file of 50,000,000 samples per channel and a sparse 64 GiB file. Each figure
is min / median / max of five runs of the production adapter and session, taken from the
pass with less background load (other projects compiled on the same host at times).
Evidence: `build-i2i3/b/measure/` at the workspace root. The builds measured did not
yet verify each compressed fragment's stream trailer, which the landed reader does in
one more finalization phase per fragment; the figures are not repeated on it.

| Channel | Overview | Window, 1 s view | Window, 4.15 M samples | Peak working set / commit with that window |
|---|---|---|---|---|
| Row layout, 10.08 GB file, f64 `a1` | 6.90 / 7.85 / 8.52 s | 3.5 / 4.5 / 5.0 ms | 88 / 93 / 107 ms | 82.1 / 71.4 MiB |
| Column, stored-clock value (remote f64 master, transposed deflate) | 12.51 / 12.93 / 13.94 s | 7.5 / 8.0 / 9.6 ms | 117 / 124 / 159 ms | 86.0 / 75.2 MiB |
| Column, virtual-clock value | 6.01 / 6.09 / 6.58 s | 3.4 / 4.1 / 5.1 ms | 64 / 66 / 66 ms | 83.9 / 73.1 MiB |
| Unsorted, channel `a` | 1.09 / 1.19 / 1.21 s | 429 / 434 / 437 ms | 363 / 371 / 393 ms | 81.9 / 70.4 MiB |

- Feeding the overview builder took the row channel's scan from 6.94 / 7.32 / 8.99 s
  with a counting visitor to 7.97 / 8.13 / 10.86 s; the whole session, scan in a worker
  and result installed, stayed within that spread (the table's figure, same pass). With
  only the overview shown the process peaks stay below 22 MiB of working set and 10 MiB
  of commit, against about 15 MiB and 8.4 MiB once the file is open.
- An unsorted window rescans the group from its start: the 1 s view sits in the
  middle of the recording.
- Cancellation, from the selection, view or close that makes a scan obsolete to the
  return of its task: 0.2–1.6 ms on a selection change during an overview, 0.2–3.1 ms
  on a zoom during a window, 0.3–1.0 ms for closing a tab during an overview; up to
  14 ms while up to ten compilers ran beside it.
- The sparse 64 GiB file opens in 3.9–6.7 ms. Its channel of 2,097,152 samples stored
  60 GiB into the file overviews in 20–38 ms and fits one window, read in 25–56 ms;
  its channel of 8,050,966,525 samples overviews in 75 s (one run) with peaks of
  17.6 MiB working set and 8.4 MiB commit, and a window at sample 8,050,966,324 reads
  in 34 ms. That file proves 64-bit offsets and indices, not throughput.

None of these figures is measured on the 16 GB reference laptop, whose page cache
does not hold a 10 GB recording between scans.
