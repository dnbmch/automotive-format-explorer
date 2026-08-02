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

The model trims mismatched vectors to their common length at the boundary. It
assumes the domain is monotonic, as guaranteed by recording backends. Domain
metadata prevents record-index fallback or a future non-time producer from being
mislabelled as seconds.

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

Opening an MDF4 file extracts only its metadata graph. Selecting a decodable
channel starts `decodeChannel(path, group, channel, firstSample, sampleCount)`
on a worker with the metadata-derived range. Completed series are cached by
group/channel. Every selection advances a generation token; a completion whose
token is no longer current is discarded, including its cache entry, so rapid
selection changes cannot flash or retain stale data.
