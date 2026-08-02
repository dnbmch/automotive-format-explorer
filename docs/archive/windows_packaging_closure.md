# Windows packaging — dependency closure and publish gate

## Problem

Windows packaging hand-lists DLL filenames with versions baked in and silences
every copy failure (`cp ... 2>/dev/null || true`). When msys2 rolled ICU 76 → 78
the three ICU copies failed silently, CI stayed green, and a zip that cannot
reach `main()` was published as a release.

The hand list is wrong in four independent ways:

- Version-pinned ICU names go stale on every msys2 roll.
- `Qt6QmlMeta.dll` did not exist when the list was written; Qt 6.9 split it out
  of `Qt6Qml`.
- Whole QML module trees are copied with `cp -r`, dragging in plugin DLLs whose
  backing libraries (`Qt6QuickControls2*StyleImpl`, `Qt6Labs*`, `Qt6QuickShapes`)
  are not on the list.
- `explorer-mdf4-backend.dll` is built but never copied, so MDF4 cannot open.

Underneath all four: **the packaged configuration is never run on a dev machine.**
Local builds use standalone Qt (`C:/Qt/<ver>/mingw_64`); the release packaged
msys2's Qt. Only the local one was ever launched by a person.

## Design

### One Qt everywhere

The release build uses the same standalone Qt as local development, installed in
CI via `install-qt-action`. msys2 keeps supplying gcc, ninja, cmake, and
protobuf. This is already known to link — `build.sh` does exactly this.

Two consequences:

- Official Qt mingw builds ship **no ICU**. The entire ICU failure class
  disappears rather than being re-pinned.
- Official Qt bundles `libgcc_s_seh-1.dll` / `libstdc++-6.dll` from its own
  older MinGW, while we compile with msys2 GCC 15. The msys2 runtime must land
  **last** so it overwrites Qt's. Newer libstdc++ serves older-built consumers;
  the reverse does not.

### Packaging by closure, not by list

`scripts/package_windows.sh` is the single packaging path, used by CI and
runnable locally. It never names a dependency:

1. Copy the executable, `explorer-core.dll`, and **every** `explorer-*-backend.dll`
   by glob, plus `samples/`.
2. Run `windeployqt --qmldir qml` for Qt libraries, platform/style/imageformat/TLS
   plugins, and QML modules. Pass `--no-compiler-runtime` so Qt's MinGW runtime is
   not dropped in.
3. Walk the dependency closure: `objdump -p` every binary under `dist/`
   breadth-first, resolve each import against `dist/`, then `C:/Windows/System32`
   (system DLL, skip), then Qt's and msys2's `bin/`. Copy what is found and
   enqueue it; anything unresolved is recorded.
4. Fail non-zero if the closure is incomplete.

Step 3 running after step 2 is what makes the msys2 GCC 15 runtime overwrite
Qt's — the ordering is load-bearing, not incidental.

`cmake/DeployMsys2Deps.cmake` keeps its own list; it serves build-tree runs, not
the package, and is out of scope here.

### Launch gate

`scripts/smoke_windows.sh` runs the packaged executable with
`QT_QPA_PLATFORM=offscreen`. A missing DLL fails the loader immediately; a root
object that will not instantiate reaches `objectCreationFailed` and exits `-1`
(`src/main.cpp`). Surviving the timeout is the pass condition. No application
change is required.

The closure check and the launch gate catch disjoint faults: the first covers
import tables, the second covers whether the deployed Qt can actually start a
QML engine and build the window.

The application's QML is compiled into the binary by qmlcachegen, and the
modules it imports self-register from the linked Qt libraries — the executable
imports `Qt6Qml`, `Qt6Quick`, and `Qt6QuickControls2` directly. The deployed
`qml/` tree is therefore not load-bearing, which is why the old `cp -r` of QML
module directories contributed most of the unresolved imports: it packaged
plugins for modules the application never loads. `windeployqt --qmldir` is kept
as the supported deployment mode; trimming the redundant tree is a size question
tracked in the backlog, not a correctness one.

### A broken build cannot publish

Both platform jobs upload workflow artifacts instead of creating the release. A
`publish` job gated on `needs: [windows-mingw, linux]` downloads both and creates
the GitHub release.

If any job fails — build, package, closure, or smoke — no release object is ever
created, so there is nothing to download. This replaces the previous shape, where
each platform published independently and a failing Windows job still left a
release standing.

CI runs the same package and smoke steps on push, so the fault surfaces before a
tag is cut rather than after.

## Success criteria

- `scripts/package_windows.sh` produces a `dist/` with an empty unresolved set,
  verified locally before any push.
- `scripts/smoke_windows.sh` passes against that local `dist/`.
- The package contains four `explorer-*-backend.dll` files.
- `dist/libstdc++-6.dll` is msys2 GCC 15's, not Qt's.
- A forced failure in either platform job leaves no GitHub release behind.
