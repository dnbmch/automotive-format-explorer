# automotive-format-explorer — handoff

## 2026-09-23 — MDF4 sessions read through one retained reader — ACCEPTED

Workspace cleanup batch I1, accepted by parent review with its relative-path
follow-up — [review](../../docs/audit/mdf4_reader_review.md#follow-up-acceptance-2026-09-25).
`Mdf4Adapter::load()` opens one `mdf4::Reader`; the session holds its metadata
(aliasing `shared_ptr`) and a read function bound to it, runs one read at a time
with one replaceable pending selection, shows failures with their reason and never
caches them, and joins its running read on destruction. The per-channel watchers,
the in-flight set, the path-based decode seam and the array trimming are gone.
Contract: [architecture](arch/architecture.md#mdf4-reads). Parity, timing and
mutant evidence: [review](../../docs/audit/mdf4_reader_review.md).

Verified: standalone PACKAGE build against a fresh parser prefix 9/9;
`tst_mdf4documentsession` 15 cases, 25 repeated runs clean; `tst_mdf4writerfile`
3 cases on the real sample. A pre-change probe shows the old session running three
reads at once for A → B → C, reading B in A → B → A, and showing a failed read as an
empty plot; four mutants (watcher left on the open worker, active read released
after notifying, no join, one read per selection) each fail their case.

Landmines: `tst_mdf4documentsession` also synchronises on
`QThreadPool::globalInstance()->waitForDone()` (`settle()`), only while no read is
blocked. Closing a session from inside the plot model's reset or series
notifications is unsupported (a Qt model cannot be destroyed while emitting); the
completion's last notification is the busy change. Runtime provenance is
load-bearing in the ctest `PATH`, the packaging closure order and
`--search`-before-`--provided` ([build reference](ref/cmake_build_system.md)); any
script CMake or Ninja invokes must pin its own msys runtime's tools first.

UNVERIFIED — no CI has run: Linux and macOS builds of the static composition and
the MDF4 session; the Linux AppImage packaging and launch gate; a headless Windows
launch of the package (BL-K6: the gate can pass on a fatal-error dialog).

UNVERIFIED — operator-visual, one Windows launch of a fresh build:
- File > Open lists "Automotive files (*.a2l *.dbc *.ldf *.mf4)", then A2L, DBC,
  LDF, MDF4 and All files, each narrowing the listing.
- The empty sidebar's four sample links open; long tab names elide with "…".
- `demo_recording.mf4`: `speed` plots the sine with zoom, pan, hover and reset; `t`
  shows the master-channel notice.
- A foreign `.mf4`: numeric channels plot; exotic ones say "not plottable".
- A large recording: clicking several channels quickly settles on the last one
  clicked, with no intermediate channel flashing. After truncating or appending to
  the open file from outside, an unviewed channel reads "Samples could not be read:
  source file changed since it was opened; reload it". Closing the tab while a long
  channel loads may freeze the window until that read returns, then carries on
  without the tab.
- Closing the window during a large A2L load exits after the parse.
- Memory grid: clicking an object selects it.

Fail = missing or unfiltered entries, a dead sample link, clipped tabs, an empty
`speed` plot or an empty plot without explanation, a stale channel shown last, a
stuck busy veil, a crash, or a hang outlasting the parse or read.

Open: the operator's verdict on a real-world `.mf4`. If one comes back mostly
non-plottable and matters, dump it with the parser's `mdf4_json`, map each
non-decodable channel class to the reader increment that unlocks it (VLSD,
unsorted, arrays, MLSD, bus logging) and spec those increments as a locked plan;
reader breadth is bought, not assumed.

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
SOURCE suite 227/227 before the current-tab check; fresh Windows package with no
Explorer DLL, complete closure and msys2 runtime provenance; the packaged app
launched with only System32 on `PATH` showed its window for 15 s. The first pass's
headless smoke result is void — the package has no offscreen platform plugin
(BL-K6). Reproducers and identities: [review](../../docs/audit/dbc_explorer_review.md).

Landmines: `tst_appcontroller` synchronises on `QThreadPool::globalInstance()->waitForDone()`
to hold a result in the finished-but-undelivered state; a future test that leaves
unrelated pool work running would make that wait cover it too. On Windows,
`qt_standard_project_setup()` already emits every executable, tests included, into
the build root.
