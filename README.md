# Automotive Format Explorer

A desktop tool for inspecting A2L, DBC, and LDF automotive files. Built with Qt/QML and C++17.

![Automotive Format Explorer](docs/screenshot.png)

## Features

- **Tree navigation** for all parsed entities (measurements, characteristics, axis points, compu methods, record layouts, units, functions, groups, typedefs, instances, variant coding, XCP/CCP protocol summaries)
- **Detail panel** with structured property cards for every entity type
- **Memory view** (A2L) -- hex grid visualization of ECU memory segments with color-coded object types, hover tooltips, and jump-to-address
- **Bidirectional selection** -- click a tree node to scroll the memory view; click a memory cell to select in tree and show details
- **Multi-tab** support for opening multiple files simultaneously
- **DBC/LDF** full tree and detail support (messages, signals, nodes, frames, schedules, etc.)

## Screenshot

> Replace `docs/screenshot.png` with an actual capture of the app. Recommended: ~1200px wide, showing all three panels with an A2L file loaded.

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).

## Current State

Built against [`a2l-parser-lib`](https://github.com/dnbmch/a2l-parser-lib), [`dbc-parser-lib`](https://github.com/dnbmch/dbc-parser-lib), and [`ldf-parser-lib`](https://github.com/dnbmch/ldf-parser-lib).

## Configure Prerequisites

- Qt 6.5 or newer with `Core`, `Gui`, `Qml`, `Quick`, `QuickControls2`, `QuickDialogs2`
- CMake 3.21 or newer
- Protobuf development package visible to CMake
- Internet access at configure time (to download parser library releases from GitHub)

No manual download or extraction of parser libraries is required. CMake automatically fetches the correct release artifacts at configure time.

## How It Works

`cmake/FetchParserLib.cmake` downloads headers and the platform-specific static library from the public GitHub releases of each `-lib` repository. Downloaded artifacts are cached under `build/_parser_deps/` so subsequent configures are instant.

## Pinning Versions

Parser library versions are set via CMake cache variables:

```cmake
-DDBC_PARSER_VERSION=v0.1.0
```

To upgrade, change the version string and delete `build/_parser_deps/<target>-<old-version>/`.

## Qt Creator

Configure should work out of the box. If it fails at the download step, check that your machine can reach `github.com` (some corporate proxies block raw GitHub downloads).