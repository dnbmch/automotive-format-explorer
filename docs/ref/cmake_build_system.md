# CMake build system

How the explorer's CMake assembles parser dependencies, deploys Windows runtime DLLs, and switches between shared and static backend models.

## Parser dependencies

[CMakeLists.txt](../../CMakeLists.txt) links the canonical targets
`a2lparser::a2lparser`, `dbcparser::dbcparser`, `ldfparser::ldfparser` and
`mdf4parser::mdf4parser`. Each target supplies its headers and archive dependencies.
MDF4's zlib dependency belongs to the producer export. Explorer separately finds
protobuf because it directly uses reflection and JSON utilities.

Standalone builds default to `AFF_PARSER_MODE=PACKAGE` and use complete installed
packages from `CMAKE_PREFIX_PATH`:

```bash
cmake -S . -B build-package -G Ninja -DAFF_PARSER_MODE=PACKAGE \
  -DCMAKE_PREFIX_PATH="/path/to/parser-prefix;/path/to/Qt"
cmake --build build-package
ctest --test-dir build-package --output-on-failure
```

`AFF_PARSER_MODE=SOURCE` requires canonical source targets supplied by the parent
workspace. From the workspace root, configure with `-DAFF_BUILD_EXPLORER=ON`.
Both modes configure offline. `EXPLORER_BUILD_TESTS=OFF` omits Qt Test discovery
and the test executables.

### Acquire complete installed packages

Each producer archive contains `include/`, `lib/`, `proto/` and
`share/<target>/build-info.json` from the same install. The identity records
source and public-header revisions, tracked modifications, compiler, platform,
build configuration and dependency versions. Keep generated headers paired with
the archive; changing the local protobuf generator cannot repair an ABI mismatch.

[scripts/acquire_parser_packages.py](../../scripts/acquire_parser_packages.py)
requires Python 3.12+ and a JSON lock whose platform entries each list the four
packages. Every package has `name`, `url` and mandatory lowercase `sha256` keys.
URLs select complete archives; hashes are computed from the actual chosen bytes.

```bash
python scripts/acquire_parser_packages.py parser-package-lock.json \
  x86_64-windows-mingw parser-prefix
```

The script verifies every hash, rejects missing or duplicate parsers and
incomplete install layouts, and publishes the combined prefix only after every
archive passes. The destination must be new. The lock is saved in that prefix.
Local `file://` archive URLs support the same verification path without network.

CI and release workflows obtain the JSON lock from the repository variable
`PARSER_PACKAGE_LOCK`, with `x86_64-windows-mingw` and `x86_64-linux-gnu` entries.
Set it to actual complete-install releases before running package CI. The app
build jobs skip while it is unset; acquisition-script tests still run. Release
jobs require the lock and fail if it is missing. Historical
split header/binary releases do not satisfy this contract. New package publication
and remote workflow execution are separate operator actions.

If package discovery fails, check the install prefix and its compiler/dependency
identity. If a hash fails, verify the selected bytes and URL. Neither case is
resolved by skipping verification or replacing producer-generated headers.

## Runtime provenance on Windows

The C++ runtime must come from the toolchain that compiled the binaries. A standalone Qt mingw distribution bundles its own `libgcc_s_seh-1.dll` and `libstdc++-6.dll`, built by an older MinGW than the msys2 gcc used here — msys2's `libstdc++-6.dll` exports symbols Qt's does not. If Qt's copy wins the DLL search, the loader binds a runtime older than the binaries were compiled against and the process dies with `STATUS_ENTRYPOINT_NOT_FOUND` (`0xc0000139`) before `main()`, with no diagnostic.

Three places enforce this, and all must keep doing so:

- [tests/CMakeLists.txt](../../tests/CMakeLists.txt) puts the compiler's own directory — derived from `CMAKE_CXX_COMPILER` — ahead of Qt's on the ctest `PATH`.
- [scripts/package_windows.sh](../../scripts/package_windows.sh) passes `--no-compiler-runtime` to `windeployqt`; the later closure walk fills the deliberately absent compiler runtime from msys2 before considering Qt.
- [cmake/DeployRuntimeDeps.cmake](../../cmake/DeployRuntimeDeps.cmake) gives the closure walk the compiler's own directory as `--search` and Qt only as `--provided`, so the runtime deployed next to a build-tree binary is the toolchain's.

Only binaries referencing a symbol absent from Qt's older runtime fail, so the fault appears in one target while its neighbours pass.

## The dependency closure walk

[scripts/deploy_closure.sh](../../scripts/deploy_closure.sh) is the only place a Windows runtime dependency is resolved, and it names none. It walks `objdump -p` over the binaries it is given, breadth-first (one `objdump` run per level — process spawns dominate), and resolves each import in this order:

1. already deployed — any binary the walk started from, plus anything it has copied
2. `C:/Windows/System32`, and the `api-ms-*` / `ext-ms-*` virtual names — skipped
3. each `--search` directory in turn — a hit is copied into `--dest` and walked itself
4. each `--provided` directory — satisfies the import without copying it, for a directory the loader reaches on its own

Anything left over is printed and fails the script. `--search` before `--provided` is what keeps the C++ runtime coming from the toolchain that compiled the binaries rather than from Qt's older copy of the same file name.

Both deploy paths call it:

| | roots | `--dest` | `--search` | `--provided` |
|---|---|---|---|---|
| Package ([scripts/package_windows.sh](../../scripts/package_windows.sh)) | `dist/`, scanned recursively | `dist/` | msys2 `bin`, Qt `bin` | — |
| Build tree ([cmake/DeployRuntimeDeps.cmake](../../cmake/DeployRuntimeDeps.cmake)) | the executable, `explorer-core`, and every backend | the executable's directory | the compiler's own `bin` | Qt `bin` |

### Package

`package_windows.sh` runs the walk over the whole of `dist/` once `windeployqt` has populated it, so the Qt plugins windeployqt just dropped in are walked too.

Ordering is load-bearing: `windeployqt --no-compiler-runtime` leaves the compiler runtime deliberately absent, and the walk then fills it from msys2. It does not rely on overwriting a Qt copy.

The rest of the packaging path — payload, launch gates, and release publication — is [release_packaging.md](release_packaging.md).

### Build tree

`deploy_runtime_deps()` is called from the top-level `CMakeLists.txt` and is a no-op off MinGW. It adds one `POST_BUILD` step on `automotive-format-explorer` that runs the same walk, so a freshly built binary runs without msys2 on `PATH`. The backends are loaded at runtime rather than imported, so they are passed as roots of their own; `add_dependencies` on the Windows branch guarantees they exist by then.

Qt is `--provided` here: build-tree runs resolve Qt from its own install, as ctest does, and no Qt DLL is copied into the build directory. Launching the build-tree binary by hand therefore needs Qt's `bin` on `PATH` (`<Qt>/<version>/mingw_64/bin`); the msys2 runtime and the backends are already next to it.

The step needs msys2's `bash`, located two levels above the compiler (`<msys2>/mingw64/bin/g++.exe` → `<msys2>/usr/bin/bash.exe`) rather than on `PATH`, which on Windows would also offer System32's WSL launcher. Without it the deploy warns at configure time and is skipped. `objdump` is taken from `--search` when the build environment does not put it on `PATH`.

## Backend linking model — shared vs static

Two assembly paths, selected at configure time via the `BACKENDS_STATIC` define.

| Model | Platform | What ships |
|---|---|---|
| Shared (default on Windows) | Windows MinGW | The four adapters become individual `.dll` backends (`explorer-<fmt>-backend.dll`) deployed next to the `.exe`. On the first open of a format, `AppController` constructs that backend's exact DLL name and loads it via `QLibrary`, resolving the `create<Fmt>AdapterPlugin` factory — lazy per format, no eager directory scan. The main `.exe` does not link against the parser libraries directly. |
| Static (default on Linux) | Linux | The four backend libraries are linked into the executable. `BACKENDS_STATIC` is defined as a compile flag, and [src/core/appcontroller.cpp](../../src/core/appcontroller.cpp) registers the four `extern "C" create<Fmt>AdapterPlugin()` factories at startup instead of scanning for DLLs. |

Adding another format adapter requires updating both branches — see [docs/arch/adapter_contract.md](../arch/adapter_contract.md).
