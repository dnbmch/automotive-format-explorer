# Signal plot

The signal plot is the format-neutral center view for sampled numeric data.
MDF4 is its first producer; producer-specific parsing, metadata, and sample
types stop in the document session.

## Data seam

`PlotSeries` carries signal and domain names/units plus parallel `double`
domain/value vectors (the domain vector retains the API name `time`). A producer
supplies the complete selected range with
`SignalPlotModel::setSeries()` and controls only the busy indicator with
`setBusy()`. View bounds, value bounds, cursor state, and rendering summaries
belong to the plot model.

Normalization happens at the producer's own parse boundary, in one pass over a
freshly decoded series: the parallel vectors are trimmed to their common length
and the domain is checked to be non-decreasing. A series that fails the check is
re-domained onto record indices. The model therefore binary-searches a sorted
axis without any check in the render or cursor path. Domain metadata prevents an
index domain or a future non-time producer from being mislabelled as seconds.

`setSeries()` takes an immutable shared series, so a producer's cache and the
model hold one buffer instead of a copy each.

## Rendering

The model precomputes min/max summaries in fixed-size sample blocks. For each
viewport it exposes either the visible samples or one min/max bucket per pixel
column:

- at up to roughly two visible samples per pixel, `SignalPlotItem` draws the
  direct sample polyline;
- above that density, it draws min/max vertical columns so narrow spikes remain
  visible; and
- axes and tick labels are painted in the same item, avoiding a QML object per
  sample or tick.

Summary blocks avoid rescanning every visible sample, while the cached paint
representation stays bounded by the visible pixel width. This keeps interaction
responsive for million-sample series. The zoom floor uses the smallest positive
domain spacing plus floating-point precision, so irregular recordings can still
zoom into dense bursts separated by large gaps.

## Interaction

- Mouse wheel zooms the domain axis around the pointer.
- Left-button drag pans the visible domain window.
- Hover snaps the cursor to the nearest sample and exposes its domain and value.
- Reset view restores the full domain range and recomputes the visible value
  range.

The value axis automatically follows the extrema of the visible domain range,
with padding for readability. Non-finite values do not contribute to extrema.

## MDF4 selection lifecycle

Opening an MDF4 file extracts only its metadata graph. Selecting a plottable
channel starts `decodeChannel(path, group, channel, firstSample, sampleCount)`
on a worker with the metadata-derived range; a channel whose decode is already
running is not decoded a second time.

A finished decode is always cached by group/channel — the samples are valid for
their channel whatever is selected by the time they arrive — while the plot is
updated only when that channel is still the selection. Rapid selection changes
therefore neither flash stale data nor throw completed work away.

The cache is bounded by bytes rather than entries: past a 256 MiB budget it
evicts least-recently-used channels, never the one on screen, and keeps a single
series larger than the whole budget so that channel still plots. Re-selecting a
cached channel refreshes its position.

A group's master channel carries the domain rather than a signal against it —
decoding a master returns its own samples in both time and value — so the tree
lists it as the group's axis channel, with its detail view intact, and never
decodes it.
