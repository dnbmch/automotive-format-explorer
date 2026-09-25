# Backlog — automotive-format-explorer

Items deliberately deferred or pending. Each entry says **what**, **where**, and the rough size.

## Parked operator decisions

Structural findings from /enforce coverage, awaiting a call — deliberately not applied.

### BL-E2: adapter repetition

The four adapters repeat protobuf-diagnostic translation; format identity and
construction live once, in the built-in format list. Their actual load boundaries differ: A2L/DBC/LDF own text-loader
objects while MDF4 extracts metadata directly and defers bulk samples to its
session. A generic load template would hide that distinction for little source
reduction. Keep the explicit adapters; extract only diagnostic translation if
its semantics ever need to change together.

**Size:** S for the diagnostic helper; the explicit load flows are a deliberate keep.

### BL-E3: mdf4 presenter labels for the new proto enum values

The sibling `mdf4-parser` proto gained `DataType` `UINT_BE`/`SINT_BE`/`FLOAT_BE`
and `ConversionKind` `ALGEBRAIC`/`TAB_RANGE` (plus `Conversion.formula`). The
`mdf4detailpresenter.cpp` switches don't name them and fall through to their
`"Unknown (%1)"` default — correct but unlabeled. The cases cannot be added while
the explorer compiles against the published v0.1.0 `-lib` artifacts, which
predate the enum values; add the labels (and optionally show the formula text)
in the same batch as the next parser `-lib` release pickup.

**Size:** XS, gated on a fresh `mdf4-parser-lib` release.

### BL-E4: DBC adapter declares the extraction entry point by hand

`src/adapters/dbcadapter.cpp` includes `dbc/dbcfile.h` and re-declares
`dbc::extract::extractFile` locally instead of including the public
`dbc/extract.h`, so a signature change in the parser would surface as a link error
rather than a compile error. The A2L adapter already includes its extraction
header directly; do the same here, inside the existing `signals` macro guard.

**Size:** XS.

## Packaging / release

### BL-K1: the deployed `qml/` tree is redundant

`windeployqt --qmldir qml` (`scripts/package_windows.sh`) copies QML module
directories that the application never loads. Its QML is compiled into the
binary by qmlcachegen, and the modules it imports self-register from the linked
Qt libraries — the executable's import table names only `Qt6Qml`, `Qt6Quick`,
and `Qt6QuickControls2`. Removing the whole `dist/qml` tree still leaves the app
launching cleanly under the smoke test. Dropping `--qmldir` would shed most of
the packaged file count, but the tree is also what pulls the Controls style and
Dialogs plugins in, and those are reached lazily on user action — paths the
headless smoke test never exercises. Needs a real interactive run with every
dialog opened before the flag is dropped.

**Size:** S to change, M to verify honestly.

### BL-K2: prove the new Linux AppImage launch gate on a runner

`scripts/smoke_linux.sh` now runs the AppImage offscreen with
`APPIMAGE_EXTRACT_AND_RUN=1`, and `release.yml` places it before artifact upload.
The script is syntax-checked locally; close this item after its first successful
Ubuntu release-workflow run.

**Size:** XS verification.

### BL-K5: a toolchain downgrade can leave a stale DLL in the build tree

`scripts/deploy_closure.sh` refreshes already-deployed files with `cp -u`
(mtime comparison), because a content compare is not available in every
invocation environment (msys2's `base` set ships no `cmp`). A pacman
*downgrade* that restores an older mtime therefore leaves the newer, stale
copy in place for build-tree runs. Packaging is unaffected — `dist/` is
rebuilt from scratch every time. Workaround on a downgrade: delete the
deployed DLLs next to the build-tree executable and rebuild.

**Size:** XS if msys2 ever ships `cmp` in base; otherwise accept.

### BL-K6: the Windows launch gate can pass on a fatal-error dialog

`scripts/smoke_windows.sh` forces `QT_QPA_PLATFORM=offscreen`, but `windeployqt`
deploys only `platforms/qwindows.dll`. Unless the environment points Qt at its own
plugin directory — CI runs after `install-qt-action`, whose default environment
setup is expected to, though no runner log was checked — the packaged app cannot
create its platform and hits Qt's fatal error. Depending on
how it is launched, that either fails fast (`0xC0000602`) or leaves a
"automotive-format-explorer" error box up, which survives `SMOKE_SECONDS` and
passes. Reproduced locally; the release_packaging.md claim that
every startup fault exits before the timeout does not hold for this case. Decide
between deploying the offscreen plugin and running the gate without CI's Qt
environment, or asserting the real window instead of survival.

**Size:** S.
