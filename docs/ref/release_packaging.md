# Release packaging

The explorer ships as a Windows zip and a Linux AppImage, built from the workspace
sources and published by one command ([Releasing](#releasing)). Every package
resolves its own dependency closure and opens every bundled sample in its launch
gate before it is published, and again after it is downloaded from the release.

## One Qt everywhere

Both platforms build against Qt 6.10.1 from Qt's own binaries: the standalone
install on the workstation that `build-all.sh` and `build.sh` name, and aqtinstall's
in the workspace's Linux image on srv-one. On Windows msys2 supplies gcc, ninja,
cmake and protobuf; on Linux Ubuntu 24.04 supplies them.

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
packaging path; the release and local runs invoke it:

```bash
QT_PREFIX=C:/Qt/6.10.1/mingw_64 bash scripts/package_windows.sh <build> <dist>
```

`QT_PREFIX` names the standalone Qt `mingw_64` root. `MINGW_BIN` names the msys2
`mingw64/bin`; it defaults to `$MINGW_PREFIX/bin` inside an msys2 shell and
otherwise to the directory of `g++` on `PATH`. msys2 need not sit at one absolute
path, so the toolchain is located, never hardcoded.

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
disagree with the binaries. An import that resolves nowhere exits non-zero and
fails the package.

The package is unsigned, so Windows SmartScreen shows "Windows protected your PC"
when a downloaded copy first starts; "More info → Run anyway" starts it.

## The Linux package

[scripts/package_linux.sh](../../scripts/package_linux.sh) is the single Linux
packaging path; srv-one's check runs it in the workspace's Linux image, which sets
`QT_PREFIX`:

```bash
VERSION=dev bash scripts/package_linux.sh <build> <out>
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

The expected filenames come from the built-in format list, not directory contents:
removing any sample fails the check. An unopenable or invalid MDF4 still produces
an interactive diagnostic tab, but the check reports its opening error and fails.
Recoverable file diagnostics, including the bundled DBC's dangling `VAL_`, remain
successful opens. `tst_check` exercises the actual executable with complete, missing
and corrupt sample payloads and with the four formats under non-ASCII directory and
file names; `tst_builtinformats` checks that failure diagnostics remain available in
their tabs.

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
a development shell puts Qt's `bin` on `PATH` and its plugins in
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

## Releasing

`bash release-explorer.sh vX.Y.Z [--notes FILE] [--dry-run]` at the workspace root
releases, on the operator's instruction, from the workstation in Git Bash with `gh`
logged in and `ssh srv-one` working. It stops at the first failure, so nothing is
published unless every step before it passed:

1. The tag is free, and every repository, each parser's `lib` and
   `aff-release-manifest` is committed and at its pushed head.
2. Windows: `build-all.sh` builds and tests the workspace in Release; then
   `scripts/package_windows.sh`, `scripts/smoke_windows.ps1` and the zip, `dist/`'s
   contents at its root.
3. Linux: srv-one's `linux/check.sh` builds and tests the same commits, which the
   script compares with the workstation's, and packages and gates the AppImage
   ([Linux on srv-one](../../../docs/ref/local_toolchain.md#linux-on-srv-one)).
4. The notes: FILE, or the commit subjects since the previous tag, docs and tests
   left out; the standard downloads section follows either.
5. `gh release create` publishes `automotive-format-explorer-<tag>-windows-x64.zip`
   and `automotive-format-explorer-<tag>-linux-x86_64.AppImage`.
6. Both are downloaded from the release and gated again: the zip here, the AppImage
   in srv-one's build container.
7. The `aff-release-manifest` entry records every repository's commit and is pushed.

`--dry-run` stops after step 4 and leaves the packages and notes in
`build-release/release`. A release needs no parser `-lib` publication: the parsers
are compiled in from their sources.

## What a package must satisfy

- `scripts/package_windows.sh` produces a `dist/` with an empty unresolved set.
- `scripts/smoke_windows.ps1` passes against that `dist/`.
- `dist/` holds no Explorer library: the executable imports only Qt, toolchain,
  protobuf/Abseil/zlib and system DLLs.
- `dist/libstdc++-6.dll` and `dist/libgcc_s_seh-1.dll` are msys2's, not Qt's.
- `scripts/package_linux.sh` produces the AppImage, and `scripts/smoke_linux.sh`
  passes against it, in srv-one's check.
- The AppImage's executable names `libGL.so.1` and not `libOpenGL.so.0`, and it
  starts on srv-one's desktop, which has no `libopengl0`.
- A failure on either platform stops the release before anything is published.
