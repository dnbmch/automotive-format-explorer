# Automotive Format Explorer

Qt/QML desktop explorer for automotive file formats, built against the precompiled `-lib` parser packages.

## Current State

The first implementation slice currently wires `dbc-parser-lib` into the explorer. `a2l-parser-lib` and `ldf-parser-lib` are planned next on the same session/model pattern.

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
