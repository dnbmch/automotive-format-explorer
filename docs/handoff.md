# automotive-format-explorer — handoff

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
