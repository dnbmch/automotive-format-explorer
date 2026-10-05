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

- MDF4 structure browsing, metadata details, and single-channel plotting of whole
  recordings (a bounded overview plus exact windows of the view) are implemented.
  A release carrying them, real-recording acceptance and further viewer increments
  are the [remaining MDF4 viewer scope](docs/plans/mdf4_viewer.md).
- New-format backends follow the workspace parser-research priority once their
  parsers exist: ARXML → ODX → FIBEX. Each plugs in as a `FormatAdapter` +
  `DocumentSession` with its own tree/detail/center wiring.

## View richness

Remaining memory-grid richness (see
[docs/plans/memory_view_planned.md](docs/plans/memory_view_planned.md); overlap
hatching, byte-range selection, and the record-layout/conversion tooltip are
built — [docs/ref/memory_view.md](docs/ref/memory_view.md)):

- Sub-byte subdivided / half-filled cells for bit-mask footprints.
- Overlap disambiguation popup when multiple objects claim the same address.
- Detail-panel listing of every object in a selected byte range.

## Packaging

CI (Windows MinGW + Ubuntu) and `release.yml` (Windows zip + Linux AppImage)
already exist. Release builds acquire the complete installed parser packages pinned
in the `PARSER_PACKAGE_LOCK` repository variable
([build reference](docs/ref/cmake_build_system.md#acquire-complete-installed-packages)),
so a release can only be cut once complete packages of all four parsers are
published and pinned.
