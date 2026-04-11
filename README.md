# Automotive Format Explorer

A desktop tool for inspecting **A2L**, **DBC**, and **LDF** automotive files. Built with Qt/QML and C++17 by [Danube Mechatronics](https://danube-mechatronics.com).

> **[Download the latest release](https://github.com/dnbmch/automotive-format-explorer/releases)** — prebuilt binaries for Windows.

## Screenshots

### A2L — ECU Memory Map
![A2L Memory View](docs/screenshot_a2l.png)

### DBC — CAN Signal Map
![DBC Signal Map](docs/screenshot_dbc.png)

### LDF — LIN Signal Map
![LDF Signal Map](docs/screenshot_lin.png)

## Features

### Tree Navigation
Browse every parsed entity in a structured tree: measurements, characteristics, axis points, compu methods, record layouts, units, functions, groups, typedefs, instances, variant coding, and XCP/CCP protocol summaries.

### Detail Panel
Structured property cards for every entity type. Click any tree node to see all its fields, references, and metadata. Toggle raw JSON view to see the underlying protobuf data.

### Memory View (A2L)
Visual hex grid of ECU memory segments. Each byte is color-coded by the object that occupies it — characteristics (VALUE, CURVE, MAP, CUBOID, ASCII, VAL_BLK), measurements, and axis points. Features include:
- Segment selector with synthetic fallback when no segments are defined
- Hover tooltips with object name, type, address, and size
- Jump-to-address field
- Configurable bytes-per-row (8, 16, 32)
- Alternating shades to distinguish adjacent same-type objects
- Tiered size calculation (exact for VALUE/CURVE/MAP, approximate for complex layouts)

### Signal Map (DBC / LDF)
Bit-level visualization of CAN and LIN message payloads. Each signal is rendered at its exact bit position with correct big-endian or little-endian layout. Features include:
- Color-coded signals with alternating shades for adjacent signals
- Multiplexor group filtering
- Overlap detection
- Hover tooltips with factor, offset, range, and unit
- Keyboard navigation

### Bidirectional Selection
Click a tree node and the center view scrolls to it with a highlight flash. Click a cell in the memory or signal view and the tree scrolls to that entity, with the detail panel updating simultaneously.

### Multi-Tab
Open multiple files side by side. Async file loading keeps the UI responsive.

## Parser Libraries

The explorer is built on top of three open-source parser libraries:

| Format | Parser Library | Description |
|--------|---------------|-------------|
| A2L | [a2l-parser-lib](https://github.com/dnbmch/a2l-parser-lib) | ASAP2 / ASAM MCD-2MC calibration files |
| DBC | [dbc-parser-lib](https://github.com/dnbmch/dbc-parser-lib) | Vector CAN database files |
| LDF | [ldf-parser-lib](https://github.com/dnbmch/ldf-parser-lib) | LIN Description Files |

All three parse their respective formats into Protocol Buffer messages. The explorer fetches prebuilt release artifacts automatically at CMake configure time.

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).

The parser libraries are dual-licensed (GPL / Commercial). See their respective repositories for details, or contact [Danube Mechatronics](https://danube-mechatronics.com) for commercial licensing.

## Building from Source

### Prerequisites

- Qt 6.5 or newer (`Core`, `Concurrent`, `Gui`, `Qml`, `Quick`, `QuickControls2`, `QuickDialogs2`)
- CMake 3.21+
- Protobuf development package
- Internet access at configure time (parser libraries are downloaded automatically)

### Build

```bash
cmake -B build -G Ninja
cmake --build build
```

### Pinning Parser Versions

```cmake
cmake -B build -DA2L_PARSER_VERSION=v0.2.0 -DDBC_PARSER_VERSION=v0.2.0 -DLDF_PARSER_VERSION=v0.3.0
```

To upgrade, change the version and delete `build/_parser_deps/<target>-<old-version>/`.
