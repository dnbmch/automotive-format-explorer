# Release packaging

The explorer ships as a Windows zip and a Linux AppImage, produced either by
[.github/workflows/release.yml](../../.github/workflows/release.yml) on a `v*`
tag from pinned parser packages or [from the workspace sources](#release-from-sources).
Every package resolves its own dependency closure and opens every bundled sample in
its launch gate before a release object exists.

## Parser package inputs

App builds acquire complete parser install archives before configuring CMake.
The repository variable `PARSER_PACKAGE_LOCK` supplies platform-specific URLs
and mandatory SHA256 pins. Parser headers and archives stay paired with their
producer identity. See [package selection](cmake_build_system.md#parser-dependencies).
CI app jobs skip until this explicit input is set; acquisition-script tests run
independently. Release jobs require the lock and cannot publish without a
successful package build. Source workspace builds need no release artifacts.

## One Qt everywhere

Every CI and release job and local development build against Qt 6.10.1 from Qt's
own binaries, installed in CI by `install-qt-action`
([.github/workflows/ci.yml](../../.github/workflows/ci.yml),
[.github/workflows/release.yml](../../.github/workflows/release.yml)) and on srv-one
by aqtinstall in the workspace's Linux image. On Windows msys2 supplies gcc,
ninja, cmake, and protobuf, and `build.sh` does the same locally; on Linux Ubuntu
24.04 supplies them.

Two consequences carry the Windows packaging design:

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

The package is unsigned, so Windows SmartScreen shows "Windows protected your PC"
when a downloaded copy first starts; "More info → Run anyway" starts it.

## The Linux package

[scripts/package_linux.sh](../../scripts/package_linux.sh) is the single Linux
packaging path — CI, the release job and the srv-one check all invoke it:

```bash
QT_PREFIX="$QT_ROOT_DIR" VERSION="$GITHUB_REF_NAME" bash scripts/package_linux.sh build .
```

It puts the samples under `usr/share/automotive-format-explorer/samples`, where
the executable finds them, writes a desktop entry naming the four formats and
taking files (`%F`), and hands the executable to linuxdeploy and its Qt plugin,
pinned to tagged builds and run extracted, so the packaging host needs no FUSE.
linuxdeploy bundles every library the executable and the deployed Qt plugins need,
except those it leaves to the system: glibc, the OpenGL and X11 client libraries,
fontconfig and their like. The only platform plugin is xcb; a Wayland desktop runs
the application through Xwayland.

The executable links `libGL.so.1`, as Qt's own libraries do
([CMakeLists.txt](../../CMakeLists.txt) sets `OpenGL_GL_PREFERENCE` to `LEGACY`
before Qt is found). CMake's GLVND default names `libOpenGL.so.0` and
`libGLX.so.0` instead, and `libOpenGL.so.0` ships in `libopengl0`, a package a
desktop can lack — the v0.2.1 AppImage stops there with `libOpenGL.so.0: cannot
open shared object file`. The script refuses an executable that names it.

The AppImage is built on Ubuntu 24.04, the parser packages' build host, and its
binaries need at most `GLIBC_2.38`: it runs on Ubuntu 24.04 and newer and on
distributions with glibc 2.38 or newer; Ubuntu 22.04 and Debian 12 are older.
Older distributions are built on request. Its runtime mounts the image with the
system's `fusermount`, so a desktop needs no `libfuse2`.

## Launch gates

Both gates run the packaged application's own check: `automotive-format-explorer
--check` opens every bundled sample in turn and exits 0 only when each opened, the
QML engine reported no warning and the main window drew the last one
([architecture](../arch/architecture.md#opening-several-files)). Opening the samples
runs every format backend and builds every center view, so a QML module or plugin
missing from the package fails the gate, not only one the main window needs.

[scripts/smoke_windows.ps1](../../scripts/smoke_windows.ps1) runs the check on the
deployed Windows platform plugin and passes on its exit code 0. On Windows the
check also requires the main window uncloaked: [src/main.cpp](../../src/main.cpp)
cloaks the window QML creates and uncloaks it on the first swapped frame. The gate
fails on a non-zero exit, when any other visible window of the process appears, or
after its timeout (60 s by default); it stops only the process it launched, on
every path, and prints the check's report after its verdict. Each fault has its
own report:

| Fault | Report |
|---|---|
| A DLL missing from `dist/` | exit `0xC0000135` before the window |
| No platform plugin in `dist/` | Qt's fatal-error box, with its text |
| A root object that will not instantiate | exit `-1` (`objectCreationFailed`) |
| A sample that does not open, or a QML warning | exit `1`; the report names the file or the warning |

The app runs with `PATH` reduced to the system directories and no `QT_*` or
`QML*` variables, so nothing outside `dist/` can stand in for a missing file —
CI's Qt installation puts its `bin` on `PATH` and its plugin directory in
`QT_PLUGIN_PATH`. Critical-error boxes are suppressed, so a loader failure exits
instead of waiting on the desktop. The gate needs an interactive desktop.

To check the gate, copy `dist/`, delete `platforms/qwindows.dll` or
`Qt6Quick.dll` from the copy and run the gate on it: it must fail with the
report above and leave no process behind.

[scripts/smoke_linux.sh](../../scripts/smoke_linux.sh) runs the AppImage's check
as users run it, on its xcb platform plugin and Qt's default OpenGL scene graph,
here on a virtual X server (`xvfb-run`) with Mesa's software rasterizer. The
AppImage runs extracted (`APPIMAGE_EXTRACT_AND_RUN=1`); `SMOKE_SECONDS` (60 s by
default) bounds the run, and the gate passes on the check's exit code 0. It
requires exactly one packaged AppImage in the directory it is pointed at. It runs
on the build host, which has every library the build needed; a start on a stock
desktop is the srv-one pre-flight's
([local toolchain](../../../docs/ref/local_toolchain.md#linux-on-srv-one)).

The closure walk and the launch gate catch disjoint faults: the first covers
import tables, the second whether the deployed Qt starts a QML engine, builds the
window and every center view, and opens each format.

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
on both platforms on every push to `master` and `release/**`, once
`PARSER_PACKAGE_LOCK` is set, so a packaging fault surfaces before a tag is cut.
While it is unset, `release.yml`'s jobs skip and a release is cut from sources.

## Release from sources

A release needs no published parser packages: both packages come from the
workspace's source graph, which links the parsers' current sources, and are
published with `gh`. Every repository is committed and pushed first, the release
commit's README naming the version.

1. Windows, on the workstation: `BUILD_TYPE=Release bash build-all.sh` from the
   workspace root builds and tests everything; then
   `QT_PREFIX=… bash scripts/package_windows.sh <build>/automotive-format-explorer <dist>`
   and `scripts/smoke_windows.ps1 <dist>`. The zip holds `dist/`'s contents at its
   root: `automotive-format-explorer-<tag>-windows-x64.zip`.
2. Linux, on srv-one: `linux/check.sh` builds and tests the pushed heads and packages
   and gates `/opt/aff/out/automotive-format-explorer-dev-linux-x86_64.AppImage`,
   published as `automotive-format-explorer-<tag>-linux-x86_64.AppImage`
   ([Linux on srv-one](../../../docs/ref/local_toolchain.md#linux-on-srv-one)).
3. `gh release create <tag> --target <commit> --notes-file <notes> <zip> <AppImage>`
   creates the tag and the release.
4. Both assets are downloaded from the release and gated again; the
   `aff-release-manifest` entry records every repository's commit.

## What a package must satisfy

- `scripts/package_windows.sh` produces a `dist/` with an empty unresolved set,
  run locally before a tag is pushed.
- `scripts/smoke_windows.ps1` passes against that `dist/`.
- `dist/` holds no Explorer library: the executable imports only Qt, toolchain,
  protobuf/Abseil/zlib and system DLLs.
- `dist/libstdc++-6.dll` and `dist/libgcc_s_seh-1.dll` are msys2's, not Qt's.
- `scripts/package_linux.sh` produces the AppImage, and `scripts/smoke_linux.sh`
  passes against it, in the srv-one check before a tag is pushed.
- The AppImage's executable names `libGL.so.1` and not `libOpenGL.so.0`, and it
  starts on srv-one's desktop, which has no `libopengl0`.
- A failure in either platform job leaves no GitHub release behind.
