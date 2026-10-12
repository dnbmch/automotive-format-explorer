# MDF4 data viewer — remaining scope

Opening an `.mf4` file, browsing its channel groups and channels, the metadata detail
cards and single-channel plotting of whole recordings are built. Contracts:
[MDF4 reads](../arch/architecture.md#mdf4-reads), [signal plot](../ref/signal_plot.md);
the reader is [mdf4-parser](../../../mdf4-parser/docs/arch/reader.md). This plan holds
what is not built.

## Real-recording acceptance

- Open an `mdf4-writer` output file, plot a channel and compare its values with
  `mdf4-writer/tools/mdf_roundtrip.py`.
- Open a foreign recording (an ASAM example or a real file): its structure shows in
  full, numeric channels plot, text and array channels are described but not
  plotted, the others are listed as not plottable with their reason. The reader reads
  array channels; the Explorer describes their type, shape and axes
  ([signal plot](../ref/signal_plot.md#mdf4-selection-lifecycle)), and their value
  table is [BL-V6](../backlog.md#bl-v6-an-array-channels-values-are-not-shown).
- If a real recording that matters comes back mostly non-plottable, dump it with the
  parser's `mdf4_json` example, map each non-decodable channel class to the reader
  increment that unlocks it (the increments in `mdf4-parser/roadmap.md`) and
  spec those increments as a locked plan.

## Viewer increments

Additive; spent when a concrete showcase moment exists.

- Multi-channel overlay.
- Export of plotted data.
- A TDMS backend: a new reader and session feeding the same plot stack.
- Live view in the product apps: a lift of the plot stack, which is why it takes
  operator-authored changes only ([signal plot](../ref/signal_plot.md)).
