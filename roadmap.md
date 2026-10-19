# automotive-format-explorer — roadmap

Direction and planned work. Current behaviour is documented in
[docs/arch/architecture.md](docs/arch/architecture.md); concrete deferred items
live in [docs/backlog.md](docs/backlog.md).

## Direction

A single-binary Qt/QML viewer that opens automotive description and recording
files (A2L, DBC, LDF, MDF4) through statically linked per-format backends and
renders them with a tree view, detail panel, and format-specific center views
(A2L memory map, DBC/LDF signal map, MDF4 signal plot). The backend seam
(`FormatAdapter` / `DocumentSession`, with the format's own presenter) plus one entry in the
built-in format list is how a format arrives, not a change to the shell.

## Format coverage

- MDF4 structure browsing, metadata details, single-channel plotting of whole
  recordings, and text, Byte, frame and array tables are implemented. The recorded
  release carries MDF4 browsing and plotting; the tables are on the source head.
  Real-recording acceptance and further viewer increments are the
  [remaining MDF4 viewer scope](docs/plans/mdf4_viewer.md).
- New-format backends follow the [workspace strategy](../docs/plans/strategy_2026h2.md):
  concrete evaluation/outreach demand gates further format work, ODX before ARXML.
  No new format is queued. Each selected parser plugs in as a `FormatAdapter` +
  `DocumentSession` with its own tree/detail/center wiring.

## View richness

Remaining memory-grid richness (see
[docs/plans/memory_view_planned.md](docs/plans/memory_view_planned.md); overlap
hatching, byte-range selection, and the record-layout/conversion tooltip are
built — [docs/ref/memory_view.md](docs/ref/memory_view.md)):

- Sub-byte subdivided / half-filled cells for bit-mask footprints.
- Overlap disambiguation popup when multiple objects claim the same address.
- Detail-panel listing of every object in a selected byte range.
