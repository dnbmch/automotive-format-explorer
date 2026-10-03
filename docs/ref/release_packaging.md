# Release packaging

The explorer ships as a Windows zip and a Linux AppImage, both produced by
[.github/workflows/release.yml](../../.github/workflows/release.yml) on a `v*`
tag. Every package resolves its own dependency closure and is launched headless
before a release object exists.

## Parser package inputs

App builds acquire complete parser install archives before configuring CMake.
The repository variable `PARSER_PACKAGE_LOCK` supplies platform-specific URLs
and mandatory SHA256 pins. Parser headers and archives stay paired with their
producer identity. See [package selection](cmake_build_system.md#parser-dependencies).
CI app jobs skip until this explicit input is set; acquisition-script tests run
independently. Release jobs require the lock and cannot publish without a
successful package build. Source workspace builds need no release artifacts.

## One Qt everywhere

Windows CI, the Windows release job, and local development all build against the
same standalone Qt, installed in CI by `install-qt-action`
([.github/workflows/ci.yml](../../.github/workflows/ci.yml),
[.github/workflows/release.yml](../../.github/workflows/release.yml)); msys2
supplies gcc, ninja, cmake, and protobuf. `build.sh` does the same locally.

Two consequences carry the packaging design:

- Official Qt mingw builds ship no ICU, so no ICU library is deployed and no
  version-pinned ICU file name can go stale under an msys2 roll.
- Official Qt bundles `libgcc_s_seh-1.dll` and `libstdc++-6.dll` from an older
  MinGW than the msys2 GCC that compiles this project, so the msys2 runtime must
  own those names in the package — see
  [Runtime provenance on Windows](cmake_build_system.md#runtime-provenance-on-windows).

Because the packaged configuration is the configuration developers build and
run, a packaging fault reproduces on a workstation rather than only in a
release.

## The Windows package

[scripts/package_windows.sh](../../scripts/package_windows.sh) is the single
packaging path — CI, the release job, and local runs all invoke it:

```bash
QT_PREFIX="$QT_ROOT_DIR" bash scripts/package_windows.sh build dist
```

`QT_PREFIX` names the standalone Qt `mingw_64` root. `MINGW_BIN` names the msys2
`mingw64/bin`; it defaults to `$MINGW_PREFIX/bin` inside an msys2 shell and
otherwise to the directory of `g++` on `PATH`. msys2 does not sit at the same
absolute path on a workstation and on a CI runner, so the toolchain is located,
never hardcoded.

Three steps, in this order:

1. **Payload** — the executable and `samples/`. The Explorer core and every
   format backend are static libraries inside the executable, so a newly added
   backend ships without touching the script.
2. **Qt** — `windeployqt --qmldir qml --release --no-translations
   --no-compiler-runtime` against the copied executable.
   `--no-compiler-runtime` keeps Qt's own MinGW runtime out of `dist/`.
3. **Closure** — [scripts/deploy_closure.sh](../../scripts/deploy_closure.sh)
   over the whole of `dist/`, searching the msys2 `bin` ahead of Qt's. Every
   binary windeployqt just dropped in is walked too. Mechanics:
   [The dependency closure walk](cmake_build_system.md#the-dependency-closure-walk).

Ordering is load-bearing: step 2 leaves the compiler runtime deliberately
absent and step 3 fills it from msys2, so the runtime is deployed by provenance
instead of by overwriting a Qt copy.

Nothing in the path names a dependency. A hand-written list drifts silently —
versioned file names roll with the toolchain and Qt redistributes classes across
libraries between releases — while a closure derived from import tables cannot
disagree with the binaries. An
import that resolves nowhere exits non-zero and fails the job.

## Launch gates

[scripts/smoke_windows.ps1](../../scripts/smoke_windows.ps1) launches the
packaged executable on the deployed Windows platform plugin and passes only when
the launched process shows its main window rendered: a visible Qt window titled
"Automotive Format Explorer" that is no longer cloaked.
[src/main.cpp](../../src/main.cpp) cloaks the window QML creates and uncloaks it
on the first swapped frame, so the state proves that the platform plugin loaded,
the QML engine built the window and the scene graph drew it. No application
change serves the gate.

The gate fails when the process exits first, when any other visible window of
the process appears, or after its timeout (60 s by default); it stops only the
process it launched, on every path. Each fault has its own report:

| Fault | Report |
|---|---|
| A DLL missing from `dist/` | exit `0xC0000135` before the window |
| No platform plugin in `dist/` | Qt's fatal-error box, with its text |
| A root object that will not instantiate | exit `-1` (`objectCreationFailed`) |

The app runs with `PATH` reduced to the system directories and no `QT_*` or
`QML*` variables, so nothing outside `dist/` can stand in for a missing file —
CI's Qt installation puts its `bin` on `PATH` and its plugin directory in
`QT_PLUGIN_PATH`. Critical-error boxes are suppressed, so a loader failure exits
instead of waiting on the desktop. The gate needs an interactive desktop.

To check the gate, copy `dist/`, delete `platforms/qwindows.dll` or
`Qt6Quick.dll` from the copy and run the gate on it: it must fail with the
report above and leave no process behind.

[scripts/smoke_linux.sh](../../scripts/smoke_linux.sh) runs the packaged
AppImage with `QT_QPA_PLATFORM=offscreen` and `QSG_RHI_BACKEND=software`.
Surviving `SMOKE_SECONDS` is its pass condition, and it requires exactly one
packaged AppImage in the directory it is pointed at.

The closure check and the launch gate catch disjoint faults: the first covers
import tables, the second covers whether the deployed Qt can actually start a
QML engine and build the window.

## The deployed QML tree

The application's QML is compiled into the binary by `qt_add_qml_module`
([CMakeLists.txt](../../CMakeLists.txt)), and the modules it imports
self-register from the linked Qt libraries. The `qml/` tree that `windeployqt
--qmldir` deploys is therefore not load-bearing. `--qmldir` is kept because it
is the supported deployment mode; the size of what it copies is a backlog
question, not a correctness one.

## A broken build cannot publish

Both platform jobs upload workflow artifacts rather than creating the release. A
`publish` job gated on `needs: [windows-mingw, linux]` downloads both and
creates the GitHub release with `fail_on_unmatched_files`.

A failure in any job — configure, build, ctest, package, closure, or smoke —
means no release object is ever created, so there is nothing to download.
`release.yml` triggers only on `v*` tags.

[ci.yml](../../.github/workflows/ci.yml) runs the same package and smoke steps
on every push to `master` and `release/**`, so a packaging fault surfaces before
a tag is cut.

## What a package must satisfy

- `scripts/package_windows.sh` produces a `dist/` with an empty unresolved set,
  run locally before a tag is pushed.
- `scripts/smoke_windows.ps1` passes against that `dist/`.
- `dist/` holds no Explorer library: the executable imports only Qt, toolchain,
  protobuf/Abseil/zlib and system DLLs.
- `dist/libstdc++-6.dll` and `dist/libgcc_s_seh-1.dll` are msys2's, not Qt's.
- A failure in either platform job leaves no GitHub release behind.
