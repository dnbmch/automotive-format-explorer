# automotive-format-explorer — handoff

## 2026-09-23 — static format composition and controller-owned load shutdown — ACCEPTED

Workspace cleanup batch G1, accepted by parent review with its load-ownership
correction and current-tab notification check —
[review](../../docs/audit/dbc_explorer_review.md#final-acceptance-2026-09-25).
G2 is not started. The application composes one format list (`src/builtinformats.cpp`, target
`explorer-formats`) and moves it into `AppController`; suffix lookup, the Open
dialog filters (`AppController.fileDialogFilters`, bound in `qml/Main.qml`) and the
sample list derive from it. `explorer-core`, the four backends and `explorer-formats`
are static on every platform. Deleted: `QLibrary` loading, the `extern "C"` adapter
factories, `FormatRegistry`, `BACKENDS_STATIC`, `formatId/formatName/extensions` on
adapters, `explorercoreexport.h` with the `SignalPlotModel` import decoration, the
`WINDOWS_EXPORT_ALL_SYMBOLS` properties, and the Explorer-DLL copy in
`scripts/package_windows.sh`. `AppController::shutdown()` (from `aboutToQuit` and the
destructor, or any observer) stops opens, disconnects delivery, joins a load it still
owns and destroys an undelivered session on the GUI thread before the adapters go.
`fileLoading` is true exactly while the controller owns an unconsumed load; the
controller re-reads its state after the open/completion notification calls,
including between the current-tab helper's two signals, so an observer that shuts
the controller down from `currentTabIndexChanged` receives neither
`currentSessionChanged` nor `fileLoaded`. Contracts:
[architecture](arch/architecture.md#format-composition), [build](ref/cmake_build_system.md#static-composition),
[packaging](ref/release_packaging.md).

Verified: standalone PACKAGE build against the matching parser package 9/9
(`tst_appcontroller` 14 cases, six of them notification re-entry regressions that
each fail on the candidate they were written against); incremental workspace
SOURCE suite 227/227 before the current-tab check; fresh
Windows package with no Explorer DLL, complete closure and msys2 runtime
provenance; the packaged app launched with only System32 on `PATH` showed its
window for 15 s. The first pass's headless smoke result is void — the package has no
offscreen platform plugin (BL-K6). Commands and identities are in the
[workspace handoff](../../docs/handoff.md) and the review.

Landmines: `tst_appcontroller` synchronises on `QThreadPool::globalInstance()->waitForDone()`
to hold a result in the finished-but-undelivered state; a future test that leaves
unrelated pool work running would make that wait cover it too. On Windows,
`qt_standard_project_setup()` already emits every executable, tests included, into
the build root.

UNVERIFIED: Linux build, AppImage packaging and launch gate for the static composition
(CI has not run); a headless Windows launch of the package (BL-K6). Operator-visual, one Windows launch: File > Open lists "Automotive
files (*.a2l *.dbc *.ldf *.mf4)", A2L, DBC, LDF, MDF4, All files, each narrowing the
listing; the empty sidebar's four sample links open; closing the window during a
large A2L load exits after the parse. Fail = missing/unfiltered entries, a dead link,
or a crash or hang at exit.

## 2026-08-29 — first operator drive: two UI defects fixed, silent deploy no-op fixed, MDF4 sample bundled — OPEN

The first live click-to-plot drive surfaced defects; all fixed and committed (`5897424`
packaging, `86165ac` ui, `6aa7aa9` samples).

**Tab titles clipped** without an ellipsis — the title label sat in a plain `Row` where
`elide` never engages; it now gets a bounded width inside the capped tab (`qml/Main.qml`).

**"No samples available" everywhere** decomposed into three findings:
- Masters legitimately don't plot since the axis-channel change, but said the same thing as a
  failure. Empty plots now state why via the series' `placeholderText` — "Master channel — this
  group's time axis" / "This channel type is not plottable" / "No samples recorded" — in the
  plot area and footer ([docs/ref/signal_plot.md](ref/signal_plot.md)).
- Foreign files' exotic channels (VLSD/strings/arrays/MLSD/unsorted variants) are a reader
  coverage boundary, honestly labeled — not a defect. Operator confirmed seeing "not plottable"
  on some files; whether their numeric channels plot is still awaited.
- The C++ chain was exonerated end-to-end: the writer smoke passes against the same DLLs.

**The build-tree DLL deploy was a silent no-op** — Git for Windows' `find` shadowing msys2's on
PATH glob-expanded quoted patterns (foreign-msys-runtime command-line re-parse), the failure was
invisible inside a process substitution, and the walk printed "dependency closure complete"
having deployed nothing; masked only by DLLs the old hand-list deploy left in `build/`.
`deploy_closure.sh` now prepends its own runtime's tools and fails loudly on enumeration or
import-scan failure. Proven by deleting `zlib1.dll` and watching the walk restore it under the
hostile PATH, plus a loud negative test.

**`samples/demo_recording.mf4` bundled** (writer-authored: `t` master + `speed` sine; provenance
in `samples/SAMPLES.md`); the sidebar scanner already matched `*.mf4`. The writer smoke now runs
against it on every ctest — 7/7 Passed, no permanent skip; `MDF4_WRITER_SAMPLE` still overrides.

**Verified:** full build + ctest 7/7 Passed after every batch. `build/samples/` staged by hand
(the copy runs only on exe relink). Several commits are unpushed; the next push is also the
first CI run of the shared closure walk and the always-on writer smoke.

**Landmines:** runtime provenance is load-bearing in three places (ctest `PATH`, packaging
closure order, `--search`-before-`--provided`) — [docs/ref/cmake_build_system.md](ref/cmake_build_system.md).
Same-named tools from a different msys runtime re-parse command lines — any script CMake/ninja
invokes must pin its own runtime's tools first. BL-K5 (`cp -u` keeps a stale build-tree DLL
after a pacman downgrade) parked in [docs/backlog.md](backlog.md).

UNVERIFIED (operator-visual, one relaunch): long tab names elide with "…"; the
`demo_recording.mf4` sidebar link opens; `speed` plots the sine with zoom/pan/hover/reset; `t`
shows the master-channel notice; on foreign files numeric channels plot while exotic ones say
"not plottable". Memory-grid click-through carried. Fail = clipped tabs, missing sample link,
empty plot on `speed`, stuck busy veil.

**NEXT-SESSION KICKOFF:** operator reports the drive result. If a real-world `.mf4` comes back
mostly non-plottable and matters, dump it with `mdf4-parser/build/mdf4_json.exe`, map each
non-decodable channel class to the reader increment that unlocks it (VLSD / unsorted / arrays /
MLSD / bus logging), and spec the chosen increments as a locked plan for Opus-subagent
implementation in `mdf4-parser` with round-trip and asammdf gates — reader breadth is bought,
not assumed.
