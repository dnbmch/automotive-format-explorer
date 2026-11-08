# automotive-format-explorer — project status

Current state of play. Direction lives in [roadmap.md](roadmap.md); concrete
deferred items in [docs/backlog.md](docs/backlog.md).

## Built

- Qt 6 / QML desktop app: tree view, detail panel and a format-specific center view.
  A2L, DBC, LDF and MDF4 backends are linked statically into one executable from one
  built-in format list ([format composition](docs/arch/architecture.md#format-composition)).
- `AppController` owns the format list and the one pending load; shutdown cancels an
  MDF4 opening and waits for an A2L, DBC or LDF parse
  ([opening and shutdown](docs/arch/architecture.md#opening-files-and-shutdown)).
- Each open file is a `DocumentTab` owning its session, tree filter (`Ctrl+F`) and tree
  navigation ([tabs](docs/arch/architecture.md#tabs)).
- A2L memory map and DBC/LDF signal map as painted C++ renderers
  ([memory view](docs/ref/memory_view.md), [signal map](docs/ref/signal_map.md)).
- Format-neutral signal plot: an overview of at most 4,096 bins covers every sample,
  exact windows of at most 4 Mi samples follow the view ([signal plot](docs/ref/signal_plot.md)).
- MDF4 through one retained reader per file: a metadata-only open, a 384 MiB tree
  admission, one read at a time within a 256 MiB result allowance per open file
  ([MDF4 reads](docs/arch/architecture.md#mdf4-reads)); text, Byte, bus-log frame and
  array channels as value tables ([value table](docs/ref/value_table.md)).
- Bidirectional selection by session-local node keys; raw JSON serialized only when
  the raw view reads it ([bidirectional selection](docs/arch/architecture.md#bidirectional-selection)).
- Bundled samples with "open a sample" links ([samples/SAMPLES.md](samples/SAMPLES.md));
  `automotive-format-explorer [files...]` and `--check`
  ([opening several files](docs/arch/architecture.md#opening-several-files)); the UTF-8
  active code page on Windows ([FormatAdapter contract](docs/arch/architecture.md#formatadapter-contract));
  splash overlay + DWM cloak startup.
- Links the four canonical parser targets from complete installed packages or source
  workspace composition ([CMake build system](docs/ref/cmake_build_system.md)). GPL-3.0.
- QTest suites registered with CTest ([tests](docs/ref/tests.md)).
- Releases from the workspace sources, one command, as a Windows zip and a Linux
  AppImage, each gated by the packaged app's `--check`
  ([release packaging](docs/ref/release_packaging.md)).

## Published

The [latest release](https://github.com/dnbmch/automotive-format-explorer/releases/latest)
opens A2L, DBC, LDF and MDF4; every release and the commits in it are in
`dnbmch/aff-release-manifest`.

The README screenshots (`docs/screenshot_*.png`) show the current build: the A2L
memory map, the DBC and LDF signal maps and the MDF4 plot of a long recording.

## Deferred

No implementation is mid-flight. The operator's and the reference laptop's checks are
open in [docs/open_verification.md](docs/open_verification.md); remaining MDF4 viewer
scope is in its [plan](docs/plans/mdf4_viewer.md).

Tracked in [docs/backlog.md](docs/backlog.md): the adapter-repetition call
(BL-E2) remains parked because the loaders have distinct boundaries. Memory-
grid view-richness and new-format backends are in [roadmap.md](roadmap.md).
