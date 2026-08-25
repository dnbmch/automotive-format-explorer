# automotive-format-explorer — roadmap

Direction and planned work. Current behaviour is documented in
[docs/arch/architecture.md](docs/arch/architecture.md); concrete deferred items
live in [docs/backlog.md](docs/backlog.md).

## Direction

A single-binary Qt/QML viewer that opens automotive description and recording
files (A2L, DBC, LDF, MDF4) through a plugin-per-format backend and renders them
with a tree view, detail panel, and format-specific center views (A2L memory
map, DBC/LDF signal map, MDF4 signal plot). The plugin ABI (`FormatAdapter` /
`DocumentSession` / `DetailPresenter`) is the extension seam: new formats
arrive as backends, not as changes to the shell.

## Format coverage

- MDF4 structure browsing, metadata details, and lazy single-channel plotting
  are implemented and build against the published parser artifacts. Packaging
  MDF4 into a shipped release remains in the release phase of
  [the locked plan](docs/plans/mdf4_viewer.md).
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

## Quality & lifecycle

- Engine/controller teardown ordering hardening.
- Render documentation: per-pattern rules for the signal/memory grid stripes.

## Packaging

CI (Windows MinGW + Ubuntu) and `release.yml` (Windows zip + Linux AppImage)
already exist. Release builds fetch the parser `-lib` artifacts from GitHub
releases by their `*parser-*` asset names, so a release can only be cut once the
pinned parser tags are published.
