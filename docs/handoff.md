# automotive-format-explorer — handoff

## 2026-08-28 — plot/session and packaging backlog batch landed — DONE

BL-P1..P5, BL-K3, and BL-K4 are implemented, tested, and committed (`eba2014` packaging,
`0748633` plot/session).

**Plot/session.** Decoded series live in a 256 MiB byte-budget LRU cache that shares one
immutable buffer with the plot model (`PlotSeriesPtr`); the on-screen channel is never
evicted. Selection identity replaced the generation token: a finished decode is always
cached, only the plot update follows the current selection, and an in-flight channel is
never decoded twice. Decode results are normalized once at the session seam — arrays
trimmed, domain checked non-decreasing (NaN fails), record-index fallback on violation —
so the plot binary-searches with no hot-path checks. Group time masters are axis channels,
not plottable signals. The writer-file smoke moved to `tst_mdf4writerfile` with an exit-77
ctest skip: an unset `MDF4_WRITER_SAMPLE` now reports `Skipped`, never `Passed`.
Reference: [docs/ref/signal_plot.md](ref/signal_plot.md).

**Packaging.** `scripts/deploy_closure.sh` is the single dependency-closure walk, shared by
`package_windows.sh` and the new `cmake/DeployRuntimeDeps.cmake` (replacing the hand-listed
`DeployMsys2Deps.cmake`, which had already drifted). Qt is `--provided` in the build tree so
the msys2 toolchain keeps runtime ownership. Normative packaging facts harvested from
`docs/archive/` into [docs/ref/release_packaging.md](ref/release_packaging.md).

**Verified:** `ctest` 7/7 (6 passed + `tst_mdf4writerfile` honestly Skipped); the smoke also
run *with* a writer recording, direct and through ctest, both green. The packaging walk
reproduces the shipped v0.2.1 `dist/` file set exactly; the build-tree deploy ran as the
exe's POST_BUILD in a green build. First CI run of the shared walk happens on the next push.

**Landmines:** runtime provenance is load-bearing in three places (ctest `PATH`, closure
order in packaging, `--search`-before-`--provided` in the build-tree deploy) — documented in
[docs/ref/cmake_build_system.md](ref/cmake_build_system.md). BL-K5: `cp -u` can keep a stale
build-tree DLL after a pacman *downgrade*; `dist/` is immune.

UNVERIFIED (carried, operator-visual): the live QML click-to-plot drive — open a writer
`.mf4`, click `speed`: sine ±100 within ~0.1 s, wheel zoom, drag pan, hover readout, "Reset
view" restores; the master channel now shows as an axis entry (open circle, "Master
channel" subtitle) and must not plot. Memory-grid click-through. Fail = blank plot, stuck
busy veil, or a QML type error.
