# MDF4 data viewer — remaining scope

Opening an `.mf4` file, browsing its channel groups and channels, the metadata detail
cards and single-channel plotting of whole recordings are built. Contracts:
[MDF4 reads](../arch/architecture.md#mdf4-reads), [signal plot](../ref/signal_plot.md);
the reader is [mdf4-parser](../../../mdf4-parser/docs/arch/reader.md). This plan holds
what is not built.

## Release carrying MDF4

No explorer release carries the MDF4 backend. One needs complete installed packages of
all four parsers in `PARSER_PACKAGE_LOCK`, MDF4's with the bounded reader interface
(`scan`, `axis`, typed outcomes)
([build reference](../ref/cmake_build_system.md#acquire-complete-installed-packages)).
The explorer release is on-demand; do not create or retag an explorer v0.2.1 release.

## Real-recording acceptance

- Open an `mdf4-writer` output file, plot a channel and compare its values with
  `mdf4-writer/tools/mdf_roundtrip.py`.
- Open a foreign recording (an ASAM example or a real file): its structure shows in
  full, decodable channels plot, the others are listed as not plottable with their
  reason.
- If a real recording that matters comes back mostly non-plottable, dump it with the
  parser's `mdf4_json` example, map each non-decodable channel class to the reader
  increment that unlocks it (the deferred increments in `mdf4-parser/roadmap.md`) and
  spec those increments as a locked plan. Reader breadth is bought, not assumed.

## Viewer increments

Additive; spent when a concrete showcase moment exists.

- Multi-channel overlay.
- Export of plotted data.
- A TDMS backend: a new reader and session feeding the same plot stack.
- Live view in the product apps: a lift of the plot stack, which is why it takes
  operator-authored changes only ([signal plot](../ref/signal_plot.md)).
