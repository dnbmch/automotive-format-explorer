# Automotive Format Explorer

[![CI](https://github.com/dnbmch/automotive-format-explorer/actions/workflows/ci.yml/badge.svg)](https://github.com/dnbmch/automotive-format-explorer/actions/workflows/ci.yml)

A desktop tool for inspecting **A2L**, **DBC**, **LDF**, and **MDF4** automotive files. Built with Qt/QML and C++17 by [Danube Mechatronics](https://danube-mechatronics.com).

> **[Download latest release](https://github.com/dnbmch/automotive-format-explorer/releases/latest)**
>
> **Windows**: extract the zip and run `automotive-format-explorer.exe`
> **Linux**: `chmod +x *.AppImage && ./automotive-format-explorer-*.AppImage`
>
> The current v0.2.1 release includes A2L, DBC, and LDF. MDF4 support is on `master` and ships with
> the next release.

---

## Screenshots

### A2L -- ECU Memory Map
![A2L Memory View](docs/screenshot_a2l.png)

### DBC -- CAN Signal Map
![DBC Signal Map](docs/screenshot_dbc.png)

### LDF -- LIN Signal Map
![LDF Signal Map](docs/screenshot_lin.png)

---

## Features

### Supported Formats

| Format | Standard | Typical Use |
|--------|----------|-------------|
| **A2L** | ASAM MCD-2MC (ASAP2) | ECU calibration and measurement definitions |
| **DBC** | Vector CANdb | CAN bus message and signal databases |
| **LDF** | LIN Consortium | LIN bus network description |
| **MDF4** | ASAM MDF 4 | Measurement recordings and physical signal data |

### Tree Navigation

Browse every parsed entity in a structured tree with expand/collapse, keyboard shortcuts, and a live filter box (`Ctrl+F`) that narrows the tree to matching names — matches keep their ancestors and children visible, so filtering to a message keeps its signals in view. Supported entity types include:

- **A2L**: Modules, Measurements, Characteristics, Axis Points, Compu Methods, Record Layouts, Units, Functions, Groups, Typedef Characteristics/Structures/Axes, Instances, Variant Coding, XCP and CCP protocol summaries
- **DBC**: Messages, Signals, Nodes, Value Tables, Attribute Definitions, Environment Variables, Signal Groups
- **LDF**: Frames, Signals, Nodes (Master/Slave), Schedule Tables, Signal Encoding Types, Signal Representations
- **MDF4**: Channel Groups and Channels, each channel showing its unit and whether it is plottable (sources, storage layouts, conversions, masters, and unsupported-feature reasons live in the detail panel)

### Detail Panel

Structured property cards for every entity type, with key fields, references, and metadata. Toggle **raw JSON** view to inspect the underlying protobuf data directly.

### Memory View (A2L)

Visual hex grid of ECU memory segments. Each byte is color-coded by the object that occupies it:

| Color | Object Type |
|-------|-------------|
| Blue | VALUE (scalar calibration) |
| Teal | CURVE (1D lookup) |
| Purple | MAP (2D lookup) |
| Indigo | CUBOID+ (multi-dimensional) |
| Orange | ASCII (string parameter) |
| Cyan | VAL_BLK (value array) |
| Green | MEASUREMENT (runtime signal) |
| Gold | AXIS_PTS (standalone axis) |

- Segment selector with automatic fallback when no segments are defined
- Hover tooltips with name, type, address, computed size, record layout, and conversion
- Diagonal red hatching on bytes claimed by more than one object
- Click-drag byte-range selection with a status-bar readout (range, byte count, objects in range)
- Jump-to-address input field
- Configurable bytes-per-row (8 / 16 / 32)
- Alternating shades to distinguish adjacent same-type objects
- Tiered size calculation (exact for VALUE/CURVE/MAP, approximate for complex record layouts)

### Signal Map (DBC / LDF)

Bit-level visualization of CAN and LIN message payloads. Each signal is rendered at its exact bit position with correct big-endian or little-endian layout.

- Color-coded signals with alternating shades
- Multiplexor group filtering
- Overlap detection
- Hover tooltips with factor, offset, range, and unit
- Keyboard navigation

### Signal Plot (MDF4)

Select a numeric MDF4 channel to decode and plot physical values against its
resolved time master, or against the reader's record-index fallback when no time
master exists. File open remains metadata-only; channel samples are decoded over
an explicit range on a worker and cached after completion.

- Responsive min/max bucketing for dense and million-sample recordings
- Direct polylines at sparse zoom levels so individual samples remain exact
- Wheel zoom around the pointer and drag pan
- Nearest-sample cursor readout with correctly labeled domain, value, and units
- Unsupported channels stay browsable and explain why they are not plottable
- Late worker results are discarded after the selection changes

### Bidirectional Selection

Click a tree node and the center view scrolls to it with a highlight flash. Click a cell in the memory or signal view and the tree scrolls to that entity with the detail panel updating simultaneously.

### Multi-Tab

Open multiple files side by side. Async file loading keeps the UI responsive for large files. Each tab keeps its own tree filter.

### Bundled Samples

One sample file per text-description format ships with the app ([samples/](samples/)); when no file is open, the sidebar offers them as one-click "open a sample" links. Provenance and licenses: [samples/SAMPLES.md](samples/SAMPLES.md). The DBC sample deliberately demonstrates the diagnostics badge — it carries one dangling `VAL_` entry the parser reports as DROPPED.

---

## Parser Libraries

The explorer consumes four parser libraries from [Danube Mechatronics](https://danube-mechatronics.com), all of them published:

| Format | Library | Availability |
|--------|---------|--------------|
| A2L | [a2l-parser-lib](https://github.com/dnbmch/a2l-parser-lib) | [Releases](https://github.com/dnbmch/a2l-parser-lib/releases) |
| DBC | [dbc-parser-lib](https://github.com/dnbmch/dbc-parser-lib) | [Releases](https://github.com/dnbmch/dbc-parser-lib/releases) |
| LDF | [ldf-parser-lib](https://github.com/dnbmch/ldf-parser-lib) | [Releases](https://github.com/dnbmch/ldf-parser-lib/releases) |
| MDF4 | [mdf4-parser-lib](https://github.com/dnbmch/mdf4-parser-lib) | [Releases](https://github.com/dnbmch/mdf4-parser-lib/releases) |

Each library parses its respective format into Protocol Buffer messages. At CMake configure time the explorer pulls available parser `-lib` release artifacts pinned in `CMakeLists.txt` from GitHub. In this multi-repo workspace the pinned tags may be ahead of what is published, so the build is driven from the sibling parser working trees instead -- `bash seed-parser-deps.sh` stages them and the fetch is skipped (see [Offline build](#offline-build-sibling-working-trees)).

The parser libraries are **dual licensed: GPL-2.0 or Commercial**. See their repositories for details, or contact [Danube Mechatronics](https://danube-mechatronics.com) for commercial licensing.

---

## Building from Source

### Prerequisites

- Qt 6.5+ (`Core`, `Concurrent`, `Gui`, `Qml`, `Quick`, `QuickControls2`, `QuickDialogs2`)
- CMake 3.21+
- Protobuf development package (visible to CMake via CONFIG or MODULE mode)
- zlib development package (for MDF4 compressed data blocks)
- Parser libraries: either the sibling parser working trees staged via `seed-parser-deps.sh` (see [Offline build](#offline-build-sibling-working-trees)), or — once the pinned tags are published — internet access to fetch the `-lib` release artifacts at configure time

### Build

```bash
cmake -B build -G Ninja
cmake --build build
```

Parser library versions are pinned in `CMakeLists.txt`. To override:

```bash
cmake -B build -DA2L_PARSER_VERSION=v0.4.0 -DDBC_PARSER_VERSION=v0.4.0 -DLDF_PARSER_VERSION=v0.5.0 -DMDF4_PARSER_VERSION=v0.1.0
```

### Offline build (sibling working trees)

In the multi-repo workspace, `bash seed-parser-deps.sh` stages the sibling
parser repos' current build outputs into `build/_parser_deps/` in the exact
release-artifact layout; the configure-time fetch then skips all downloads.
Use it to build against unpublished parser changes or without network. The
seeded bits are the siblings' working trees, not the pinned releases.

### Platform Notes

- **Windows (MinGW)**: Primary development platform. Backends are shared libraries loaded at runtime.
- **Linux**: Backends are linked statically into the executable. Tested on Ubuntu 24.04 with system protobuf.

## Documentation

- [docs/arch/architecture.md](docs/arch/architecture.md) — process layout, plugin loading, DocumentSession contract, NodeRegistry lifecycle, startup splash + DWM cloak.
- [docs/arch/adapter_contract.md](docs/arch/adapter_contract.md) — how to add a new format adapter.
- [docs/ref/memory_view.md](docs/ref/memory_view.md) — A2L memory grid visual + interaction reference.
- [docs/ref/signal_map.md](docs/ref/signal_map.md) — DBC/LDF signal grid visual + interaction reference.
- [docs/ref/signal_plot.md](docs/ref/signal_plot.md) — format-neutral time-series plot reference.
- [docs/ref/keyboard.md](docs/ref/keyboard.md) — application + grid keyboard shortcuts.
- [docs/ref/cmake_build_system.md](docs/ref/cmake_build_system.md) — `fetch_parser_lib` mechanics, MSYS2 deploy, shared-vs-static backend model.
- [roadmap.md](roadmap.md) — direction and planned work.
- [project_status.md](project_status.md) — current state of play.
- [docs/backlog.md](docs/backlog.md) — known issues / planned changes.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).

The parser libraries this app loads are dual-licensed (GPL-2.0 or Commercial); they are linked via their public release artifacts, not via source dependency.
