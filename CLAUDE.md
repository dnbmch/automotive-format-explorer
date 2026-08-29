# automotive-format-explorer

Qt/QML desktop app for inspecting A2L, DBC, LDF, and MDF4 automotive files. GPL-3.0. Plugin architecture (shared `.dll` on Windows, static on Linux) with format-specific document sessions backed by `QQuickPaintedItem` C++ renderers for the memory map, signal map, and format-neutral signal plot views.

<!-- block: Guidelines Standard [id:1d67c7] -->
## Guidelines

- Keep MD files current after significant changes. Max 1256 LOC per file. This file stays orientation + guidelines — detail belongs in `docs/`
- Features and scope are the user's call — if unsure, ask. **But decide the obvious:** infer from the code, docs, and repo conventions; escalate only real design forks
- **"Check / analyze / discuss / what can we improve" = findings only.** Propose, don't edit, until told to go
- **Docs state facts, not history.** No "decided on (date)", "was X, now Y", "for now" — git tracks what was
- No pointer/stub files — move real content, update references
<!-- /block:1d67c7 -->

<!-- block: Engineering Philosophy [id:eb1d7e] -->
## Engineering philosophy

Embedded developers' mindset: sound architecture, compact code, less is more.

- **Fix causes, not symptoms.** When a defect surfaces, find the broken invariant and fix it at the right layer — a symptom patch that leaves the confusion in place is worse than none
- **No claim without `path:line`.** Statements about code behavior need a citation; on pushback, re-read the source, don't restate the guess
- Validate at system boundaries only (user input, external APIs, parsed files). No paranoid guards, "just in case" handling, or silent fallbacks between our own functions
- Three lines of clear repetition beat a premature abstraction; few well-named functions over wrapper and re-export layers
- Finish or don't start — half-done is worse than nothing. **No knowingly-bad shipped states:** fix at the right layer or park it in the backlog; never accept known-bad as the answer
- No diagnostic/warning infrastructure unless asked — fix the parser, don't reach for stderr loggers or severity enums
<!-- /block:eb1d7e -->

<!-- block: Documentation Layout [id:dc01a7] -->
## Documentation

> Names are symbolic — the live `docs/` tree wins (`ref/`≈`reference/`, `manual/`≈`user/`, `snake_case`≈`kebab-case`). Match what exists; never rename to satisfy this section.

- Root: `README.md` (what it is, how to run) · `roadmap.md` (the only active plan) · `project_status.md` (built / in flight / deferred)
- `docs/arch/` normative system facts · `docs/ref/` stable references · `docs/manual/` user-facing docs · `docs/plans/` in-progress designs · `docs/backlog.md` FIXMEs · `docs/handoff.md` last 2–3 session entries, pruned on wrap · `docs/archive/` completed material, never referenced from live docs
- Plan lifecycle: draft in `docs/plans/<name>.md` → execute → **harvest durables into `arch/`/`ref/` as soon as work lands** (never gate on tests) → move plan to `archive/` → grep out stale references. Never delete a plan, never archive it raw
- **Never pin volatile facts in prose** — line counts, LOC, test totals, source line-ranges rot on the next edit. State what a file or section *is* and *does*, not the number
- Locked decisions become facts in `arch/`/`ref/`, not standalone ADRs. Code changes touching public surface update the matching arch doc in the same commit. Doc filenames `snake_case.md` (conventional caps keep theirs)
<!-- /block:dc01a7 -->

<!-- block: Memory Discipline [id:a7b3d1] -->
## Memory discipline

- Durable knowledge lives in version-controlled project docs, not an opaque memory store. **Avoid creating memory files** — write to `docs/arch|ref/`, `project_status.md`, or `## Project notes` instead (overrides default memory behavior)
- `MEMORY.md` stays a thin index of one-line pointers; `/flush-memory` evacuates buildup
<!-- /block:a7b3d1 -->

<!-- block: Execution Rules [id:7a710c] -->
## Execution rules

**Run what the work needs** — build, test, launch, drive the app. The bounds below limit waste, not permission.

- Probe a toolchain once (`--version` / `which`) before leaning on it; if absent, say so and stop — don't grind tokens at a tool that doesn't exist
- Kill what you start — no servers, watchers, or containers left running after you report done
- Headless-browser drives (`/looky`) burn tokens fast — only when a visual change genuinely needs seeing
- Tests/migrations/seeds that hit a live service or shared DB: ask first. Host tests, type/syntax checks, `git status/diff/log`: always fine
- Never deploy, push tags, or trigger CI/release without explicit instruction
<!-- /block:7a710c -->

<!-- block: Verification & Test Debt [id:7e5701] -->
## Verification & test debt

User time is scarce and expensive; machine time is not. **Deliver code, not process** — no unrequested verification ceremony, probe harnesses, staged checklists, or sign-off theater. Honor a standing proof contract in `## Project notes`; invent no other gate.

- **Never block on the user, never nag for a manual check.** Finish the batch, commit, take the next. Untested ≠ unfinished: nothing known broken + a clean `UNVERIFIED:` line = done and committable
- Accumulate host-runnable tests. Log user-only checks as `UNVERIFIED:` lines in `docs/handoff.md` (what to check, how to tell pass from fail), grouped by shared setup, cleared on a reported result
<!-- /block:7e5701 -->

<!-- block: Agents & Delegation [id:a6e11d] -->
## Agents & delegation

- **Default lean: one agent.** Fan-outs and multi-agent review are for work explicitly scoped as an audit or genuinely parallel — if unsure which, ask
- **Tier the model to the work:** mechanical sweeps → cheap subagents (Haiku/Sonnet); fully-specced implementation → lower Opus tier; planning, orchestration, and final review → top tier only (Opus max / Fable high+)
- Brief subagents to finish — enough context and latitude to decide the obvious. Subagents return results, not narration; the orchestrator owns synthesis and the verdict
<!-- /block:a6e11d -->

<!-- block: Iterative Decision Workflow [id:1de4f0] -->
## Iterative decision workflow

Non-trivial work (refactors, features, architecture) runs: **audit → batch findings → recommend → approve → plan-audit → lock plan in `docs/plans/<name>.md` → implement in one shot**, then grep-sweep for stale refs and broken paths before reporting done.

- Audit first, concrete evidence only — no proposals until the code is mapped
- Recommend with an exact diff or pseudo-code per item; the user approves per item or batch (`go with B`, `skip 3 and 7`, `A but defer the rename`)
- **You research, the user decides** — never propose without evidence, never pick on their behalf
- Define success criteria up front, machine-checkable where possible ("fix the bug" → "failing test that reproduces it, then make it pass")
- Moves/renames update ALL references in the same session — grep, don't rely on memory. Parallelize independent work

**Presenting choices:** numbered questions, each option on its own line, one always marked `Rec:` with a one-sentence reason — signal, not a vote. Never a selection popup; the user mixes compact answers (`1a, 2b, 3 do X instead`):

> **1. \<question>?**
> - a) \<option>
> - b) \<option>
>
> ↳ **Rec: b** — \<one-sentence reason>
<!-- /block:1de4f0 -->

<!-- block: Commits & Co-Authoring [id:c0a002] -->
## Commits

- Commit when a batch wraps — coherent batches by intent, one deliverable one commit, concise messages scoped by area. **No `Co-Authored-By` lines** — authorship is the user's
- **Never push automatically.** Never `--no-verify`, never `--amend` published commits, never force-push unasked
- `[skip ci]` on doc-only or trivial batches — CI minutes cost money
- No plan-phase numbers or audit indexes in code or commit messages — name the change for what it does
<!-- /block:c0a002 -->

<!-- block: Greenfield — No Backwards-Compat [id:9e7f1d] -->
## Greenfield discipline

No external consumers, or owned end-to-end by us: the contract (proto / API / file format) is ours to change. If a change makes it better, make it — and update every callsite in the same commit. No deprecation shims, compat flags, defensive versioning, migration notes, or fictional users to protect. When real users exist, the user will say so; until then compat thinking is banned unless requested.
<!-- /block:9e7f1d -->

<!-- block: File-Path Link Convention [id:f11e7a] -->
## Referencing code & IDs

- Code → `path:line` (clickable), e.g. `src/MapPoint.h:42`
- IDs are never bare — gloss every mention inline: `RB-94 (proximity-sensor calibration drift)`. The user never opens a doc or scrolls back to decode an ID
<!-- /block:f11e7a -->

## Project notes

**Release is on-demand; there are no active users.** The app ships as a GPL-3.0 GitHub release, but we release only to exercise the current build against fresh parser artifacts — not on every change. With no users there is no cross-release backward-compat obligation; keep the plugin ABI (FormatAdapter / DocumentSession / DetailPresenter) internally coherent and change it when the design improves. See workspace [CLAUDE.md](../CLAUDE.md) "Release cadence".

### Architecture

```
                    ┌─────────────────────────────────┐
                    │       Main.qml (layout)         │
                    │  NavPanel │ CenterPanel │ Detail │
                    └─────┬─────────┬──────────┬──────┘
                          │         │          │
          ┌───────────────┘         │          └──────────────┐
          ▼                         ▼                         ▼
   TreeModel            Loader (per-session)           DetailModel
   (QAbstractItemModel)    MemoryView.qml (A2L)       (QAbstractListModel)
                           SignalMapView.qml (DBC/LDF)
                           SignalPlotView.qml (MDF4)
                                    │
                    ┌───────────────┤
                    ▼               ▼
            MemoryGridItem   SignalGridItem
            (QQuickPaintedItem, C++ rendering)
```

### Plugin architecture

Format backends are shared libraries on Windows (loaded via `QLibrary` at runtime) and static on Linux (linked at build, registered in constructor). Each backend provides:

- `FormatAdapter` — loads a file, returns a `DocumentSession`
- `DocumentSession` — owns the protobuf document, tree model, detail presenter, and optional center panel model
- `DetailPresenter` — builds `QList<DetailSection>` from a `NodeBinding`

### Center panel slot

Each `DocumentSession` exposes:
- `centerPanelSource()` — QML component URL (empty = no center panel)
- `centerPanelModel()` — data model for the center panel

Main.qml uses a `Loader` that loads the component and passes the model. When no center panel is available, the layout falls back to two columns.

### Bidirectional selection

- Tree → Detail: `AppController::selectCurrentNode(nodeKey)` → `DetailPresenter::buildDetails()`
- Tree → Center: `scrollToNodeKey(nodeKey)` on the loaded center panel component
- Center → Tree: `nodeKeyClicked` signal → `AppController::selectCurrentNode()` + `NavPanel::selectAndScrollTo()`

Node keys are assigned by `NodeRegistry` during tree construction. Memory/signal map models store the same keys for cross-referencing.

### Rendering

Both `MemoryGridItem` and `SignalGridItem` extend `QQuickPaintedItem`:

- Pre-computed flat arrays (colorMap, objectMap) for O(1) per-byte/per-bit lookup
- Paint only visible region (viewport-sized item, scroll offset in C++)
- Mouse hover, wheel, click handled in C++ — no QML MouseArea overlay
- FBO render target for best scroll performance

### Project structure

```
src/
  core/           appcontroller, noderegistry, formatid, detailsection, detailpresenter
  models/         treemodel, treefiltermodel, detailmodel, tabmodel, memorymapmodel, signalmapmodel
  sessions/       documentsession (interface), adaptersessionbase, presentertext
                  (shared text/detail helpers), a2l/dbc/ldf/mdf4 sessions
  adapters/       a2l/dbc/ldf/mdf4 adapter + factory (C plugin entry points)
  ui/             memorygriditem, signalgriditem, signalplotitem (painted renderers),
                  gridpalette (shared palette/shade/highlight-flash helpers)
qml/
  Main.qml        root layout with SplitView, tabs, Loader
  components/     NavPanel, MemoryView, SignalMapView, SignalPlotView, Theme,
                  Toast, SplashOverlay, DiagnosticsPopup
docs/             design docs, screenshots
samples/          bundled sample files (one per format) + SAMPLES.md provenance
cmake/            FetchParserLib, DeployRuntimeDeps
```

### Build

```bash
cmake -B build -G Ninja
cmake --build build
```

Qt 6.5+, CMake 3.21+, Protobuf required. Configure fetches the pinned parser `-lib` release artifacts from GitHub; in this workspace `seed-parser-deps.sh` stages the sibling parser working trees first so the fetch is skipped.

### Code conventions

See workspace [CLAUDE.md "Code conventions"](../CLAUDE.md#code-conventions-workspace-single-source-of-truth). Repo-specific exceptions:

- Qt 6 + QML is the entire UI layer. The workspace "no Qt" rule applies to the parser/library repos, not here

### CI / Release

- `ci.yml` runs on push to master: Windows MinGW + Ubuntu 24.04. The Windows job also packages and smoke-tests, so a broken package surfaces before a tag is cut
- `release.yml` triggers on `v*` tags: builds the Windows zip + Linux AppImage, then a `publish` job gated on both creates the GitHub release. A platform failure means no release object exists
- Windows CI and release build against the same standalone Qt as local development (`install-qt-action`); msys2 supplies gcc, ninja, cmake, and protobuf
- Do NOT re-tag unless the workflow is verified. Each release build takes ~3 min
- Packaging path, launch gates, and publish gating: [docs/ref/release_packaging.md](docs/ref/release_packaging.md)

### Platform differences

| | Windows | Linux |
|---|---------|-------|
| Backends | SHARED (.dll), loaded via QLibrary | STATIC, linked into exe, registered at startup |
| Qt deploy | `windeployqt` + dependency-closure walk (`scripts/deploy_closure.sh`, shared by the package and the build-tree deploy) | AppImage via linuxdeploy |
| Protobuf JSON | `google/protobuf/util/json_util.h` (stable API, works on both v3 and v4+) | |
| Define | — | `BACKENDS_STATIC` |
