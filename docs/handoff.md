# automotive-format-explorer — handoff

## 2026-08-02 — v0.2.0 Windows package was unstartable; repaired, released as v0.2.1 — DONE

The published `v0.2.0` Windows zip could not reach `main()`: `libicuin78.dll` missing. It was
not one missing DLL but **36 unresolved imports**. Packaging hand-listed DLL filenames with
versions baked in and silenced every copy failure with `cp … 2>/dev/null || true`, so an msys2
ICU 76→78 roll left the archive gutted while CI stayed green. The same list also missed
`Qt6QmlMeta.dll` (Qt 6.9 split it out of `Qt6Qml`) and never copied `explorer-mdf4-backend.dll`
at all; `cp -r` of whole QML module trees dragged in plugin DLLs whose backing libraries were
never listed, which produced ~30 of the 36.

**Packaging no longer names dependencies.** [scripts/package_windows.sh](../scripts/package_windows.sh)
deploys Qt with `windeployqt`, then walks the `objdump` closure over every binary and fails on
anything unresolved. [scripts/smoke_windows.sh](../scripts/smoke_windows.sh) launches the packaged
app headless. Windows CI and release build against the same standalone Qt as local development,
so the packaged configuration is one a developer can reproduce — official Qt mingw ships no ICU,
so that failure class is gone rather than re-pinned. Release publication moved to a `publish` job
gated on `needs: [windows-mingw, linux]`: a failing platform leaves no release object at all.

**`v0.2.1` released** (v0.2.0 + the packaging repair, MDF4 excluded — the
1b choice, because MDF4 cannot build off-workstation). Verified against the real download: 0
unresolved imports (was 36), launches with a real window under a stripped `PATH`. `v0.2.0` was
converted to a **draft**, so its broken zip is publicly 404 and `releases/latest` resolves to
v0.2.1; reversible with `gh release edit v0.2.0 --draft=false`.

**Two further defects surfaced, both caught by the new gates before publication:**

- `tests/CMakeLists.txt` put Qt's bin ahead of the compiler's on the ctest `PATH`. Standalone Qt
  bundles an older MinGW `libstdc++-6.dll` (13712 exports vs msys2's 13730), so tests bound a
  runtime older than the GCC that compiled them and `tst_a2ldetailpresenter` died with
  `STATUS_ENTRYPOINT_NOT_FOUND` before `main()`. Only the test linking the parser libraries
  referenced one of the missing symbols, so it failed alone.
- `package_windows.sh` hardcoded `/c/msys64/mingw64/bin`, absent on a CI runner, so protobuf and
  abseil resolved nowhere. Now derived from `MINGW_PREFIX`, else `g++` on `PATH`.

Durables harvested into [docs/ref/cmake_build_system.md](ref/cmake_build_system.md) — "Runtime
provenance on Windows" and the abseil-drift failure mode. Plan archived to
[docs/archive/windows_packaging_closure.md](archive/windows_packaging_closure.md). Three residual
items parked as BL-K1..K3 in [docs/backlog.md](backlog.md).

**Landmines:**

- **Runtime provenance is load-bearing in two places** — the ctest `PATH` and the packaging
  closure order. Break either and binaries bind Qt's older runtime and die before `main()` with
  no diagnostic. Documented in `ref/cmake_build_system.md`.
- **`master` has a current, narrowly explained CI failure.** Run `30772666463` reaches the MDF4
  fetch on Windows and Linux and fails only because v0.1.0 assets are unpublished.
- **This workstation's msys2 abseil (2508) lags CI's (2605)**, so the published parser artifacts
  cannot be linked locally. Local builds must run `seed-parser-deps.sh` first. A clean `build/`
  wipe re-triggers the fetch and the link fails confusingly.
- The current run stops at the known fetch gate, so build, ctest, and the new release gates still
  require a rerun after publication.

**Real-file parsing verified against the published Windows zip:** the packaged backend factories
opened `demo_ecu.a2l` (0 diagnostics), `tesla_can.dbc` (1 expected nonfatal diagnostic), and
`demo_seat.ldf` (0 diagnostics). Carried operator-visual work: memory-grid click-through and the
MDF4 click-to-plot drive.

**NEXT-SESSION KICKOFF:** provide `mdf4-parser` a fine-grained `LIB_RELEASE_TOKEN` scoped to
contents-write on `mdf4-parser-lib`; cut v0.1.0, verify/rerun both release CIs, pin the headers
sha256, run explorer master CI, and record the manifest entry. `release/v0.2.1` is retired and its
tag preserves the shipped commit. Do not re-tag v0.2.1.
