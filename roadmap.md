# automotive-format-explorer — roadmap

Direction and planned work. Current behaviour is documented in
[docs/arch/architecture.md](docs/arch/architecture.md); concrete deferred items
live in [docs/backlog.md](docs/backlog.md).

## Direction

A single-binary Qt/QML viewer that opens automotive description files (A2L, DBC,
LDF) through a plugin-per-format backend and renders them with a tree view,
detail panel, and format-specific center views (A2L memory map, DBC/LDF signal
map). The plugin ABI (`FormatAdapter` / `DocumentSession` / `DetailPresenter`)
is the extension seam: new formats arrive as backends, not as changes to the
shell.

## Format coverage

- New-format backends follow the workspace parser-research priority once their
  parsers exist: ARXML → ODX → FIBEX. Each plugs in as a `FormatAdapter` +
  `DocumentSession` with its own tree/detail/center wiring.

## View richness

The memory grid is the lagging view. Documented-but-unbuilt behaviour (see
[docs/ref/memory_view.md](docs/ref/memory_view.md)) to bring it to parity with
the signal grid:

- Hatched/striped overlap visualization and half-filled bit-mask cells in the
  memory grid (currently signal-grid only).
- Click-drag byte-range selection with an "N objects in range" status readout.
- Overlap disambiguation popup when multiple objects claim the same address.
- Richer hover tooltip (record layout / conversion fields).

## Quality & lifecycle

- Test scaffolding: `enable_testing()` + a QTest target (no automated coverage
  exists yet).
- Engine/controller teardown ordering hardening.
- Render documentation: per-pattern rules for the signal/memory grid stripes.

## Packaging

CI (Windows MinGW + Ubuntu) and `release.yml` (Windows zip + Linux AppImage)
already exist. Release builds depend on the parser `-lib` artifacts being
published under their renamed `*parser-*` names — see workspace `BACKLOG.md`
`BL-W13`.
