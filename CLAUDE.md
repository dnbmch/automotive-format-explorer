# automotive-format-explorer

Qt/QML desktop app for inspecting A2L, DBC, and LDF automotive files. GPL-3.0. Plugin architecture (shared `.dll` on Windows, static on Linux) with format-specific document sessions backed by `QQuickPaintedItem` C++ renderers for the memory map and signal map views.

<!-- block: Guidelines Standard [id:1d67c7] -->
## Guidelines

- Keep MD files up to date after significant changes
- Commit changes to git with sensible messages
- Max 1256 LOC per file — no monolithic mega files
- Do not assume, do not decide for me about features — if unsure, ask
- Less is more. KISS, DRY, readable, maintainable. Do not over-abstract
- Do not write bloated code
- Do not over-pollute CLAUDE.md — orientation + guidelines only
- **After context compaction:** do not act on assumptions from the summary. Read the summary, then ask the user what to do next
- **Docs state facts, not history.** No "decided on (date)", "was X, now Y", "for now". State what *is*. Git tracks what *was*
- **No pointer/stub files.** Don't leave "moved to ..." placeholders. Move real content, update references
<!-- /block:1d67c7 -->

<!-- block: Engineering Philosophy [id:eb1d7e] -->
## Engineering philosophy

We are embedded developers. We write sound architectures with compact, non-bloated code.

- **No patches, no hipshot fixes.** When a bug or defect surfaces, zoom out. Understand the big picture — what invariant broke, where the design assumed something it shouldn't, what else is touched. Then architect the canonical fix at the right layer. A symptom-level patch that leaves the underlying confusion in place is worse than no fix
- Trust internal invariants. Validate at system boundaries (user input, external APIs, parsed files) — not between your own functions
- No paranoid defensive code. Don't add `if (ptr != nullptr)` chains, fallback branches, or try/catch for cases that practically cannot happen
- No "just in case" error handling, no silent fallbacks that mask real bugs
- Prefer few well-named functions over deep wrappers and re-export layers
- Three lines of clear repetition beat a premature abstraction
- Half-finished implementations are worse than nothing — finish, or don't start
- No diagnostic / warning infrastructure unless asked. If extraction can't map a value, fix the extractor — don't propose stderr loggers, `parse_warnings` fields, or severity enums as a first response
<!-- /block:eb1d7e -->

<!-- block: Documentation Layout [id:dc01a7] -->
## Documentation

Root files:
- `README.md` — what the project is, how to run it
- `roadmap.md` — the only active execution plan
- `project_status.md` — current state of play: what's built, what's in flight, what's deferred

Under `docs/`:
- `docs/arch/` (or `docs/architecture/`) — normative facts about the running system. Name files for what they describe, not for phases or tasks
- `docs/ref/` — stable cross-cutting references (coordinate systems, glossaries, file formats)
- `docs/manual/` — user-facing operational documentation (how to use the app, workflows, screenshots)
- `docs/plans/` — in-progress design docs and proposals
- `docs/backlog.md` — FIXMEs and future work
- `docs/handoff.md` — optional per-session operational log
- `docs/archive/` — completed plans and historical material. Never active guidance, never referenced from live docs

Plan lifecycle — **never delete a plan, never archive it raw**:
1. Draft the plan in `docs/plans/<name>.md`
2. Execute it
3. **Before archiving, harvest everything durable**: agreed approaches, strategies, invariants, formats, conventions, rationale, lessons. Lift them into `docs/arch/` or `docs/ref/` so the knowledge survives the plan
4. Move the plan file to `docs/archive/`
5. Grep for active references to the old path and remove them

Rules:
- Doc filenames are `snake_case.md`. Conventional capitalized files keep their casing: `README.md`, `LICENSE.md`, `CHANGELOG.md`, `CLAUDE.md`
- Locked decisions become facts in `arch/` or `ref/`, not standalone ADRs
- Code changes that touch public surface (APIs, file formats, paths, proto fields) update the matching arch doc in the same commit
- Update `project_status.md` after significant changes
- No active references to anything in `archive/`
<!-- /block:dc01a7 -->

<!-- block: Don't Run Without Asking [id:7a710c] -->
## Execution rules

- Do not start dev servers (`npm run dev`, `python -m service.main`, etc.) without asking
- Do not run builds (`cmake --build`, `npm run build`, `cargo build`, ESP-IDF, Keil) without asking — the user builds manually in their IDE
- Do not deploy, push tags, or trigger CI/release workflows without explicit instruction
- Read-only checks are fine: `git status`, `git diff`, `git log`, syntax checks (`python -m py_compile`), tests when the user has asked for verification
- If unsure whether a command is read-only, ask first
<!-- /block:7a710c -->

<!-- block: Iterative Decision Workflow [id:1de4f0] -->
## Iterative Decision Workflow

For non-trivial work (refactors, new features, architecture changes), follow this loop:

1. **Spec / Audit** — read the spec or audit the codebase: relevant files, grep for impacts, map what exists. Surface concrete evidence (current state, problems, stale refs, dead code). No proposals yet
2. **Batch** — group findings into coherent batches by intent or area. Don't dump a flat list of 40 items
3. **Recommend** — for each item, propose a solution **with an exact diff or pseudo-code**, not just prose. The user must see what the change looks like before approving
4. **Approval** — wait for explicit go/no-go per item or per batch. Short answers are fine ("go with B", "skip 3 and 7", "A but defer the rename")
5. **Plan audit** — once the approved set is known, re-check it as a whole: gaps, contradictions, ordering, missed dependencies, broken references
6. **Plan finalization** — write the locked plan into `docs/plans/<name>.md` the user can review and reference during execution
7. **Implementation** — execute the locked plan in one shot with a todo checklist. Fix small problems and keep going. After execution: grep sweep for stale references, broken paths, inconsistencies — fix before reporting done

Rules:
- Research is my job, decisions are yours. Never propose without evidence, never pick for you
- Parallelize independent work
- When moving or renaming anything, update ALL references in the same session — imports, docs, configs, launch files, codegen. Grep, don't rely on memory
<!-- /block:1de4f0 -->

<!-- block: Commits & Co-Authoring [id:c0a002] -->
## Commits

- **No `Co-Authored-By` lines.** The user is responsible for what gets pushed; authorship is theirs
- **Commit when a batch wraps** with a sensible scoped message. Don't wait to be asked
- **Never push automatically.** Push is always user-driven
- **Use `[skip ci]`** in commit messages for doc-only or trivial batches — GitHub Actions minutes cost money
- Commit in coherent batches by intent — one deliverable, one commit
- Keep messages concise and scoped by area (e.g. `kernel`, `viewer`, `docs`)
- **No audit-item indexes, no plan-phase numbers in code or commit messages.** No `// Batch C.2`, no `feat: Phase 3.1 — ...`. Name the change for what it *does*, not which doc tracked it. The indexing scheme is administrative noise that rots the moment the plan is archived
- Never `--no-verify`, never `--amend` published commits, never force-push without explicit instruction
- Do not use git to roll back changes — if you are unsure it will work, the user can do that manually
<!-- /block:c0a002 -->

<!-- block: Greenfield — No Backwards-Compat [id:9e7f1d] -->
## Greenfield discipline

This project has no external consumers yet. Act accordingly:

- The proto / API / file-format contract is ours to change. Add, rename, restructure freely when it makes the model cleaner
- Do not invent fictional users to protect
- Do not add deprecation shims, compatibility flags, or "for backward compat" fields
- Do not version messages defensively
- If a change makes the contract better, just make it — and update every callsite in the same commit
<!-- /block:9e7f1d -->

<!-- block: File-Path Link Convention [id:f11e7a] -->
## File references

When referencing code in chat or docs, use `path:line` so the user can click through. Example: `src/MapPoint.h:42`.
<!-- /block:f11e7a -->

## Project notes

**Exception to Greenfield:** the app is published as a GPL-3.0 release on GitHub. Plugin ABI (FormatAdapter, DocumentSession, DetailPresenter interfaces) must stay backward-compatible within a release line — bump major version on changes.

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
  models/         treemodel, detailmodel, tabmodel, memorymapmodel, signalmapmodel
  sessions/       documentsession (interface), adaptersessionbase, a2l/dbc/ldf sessions
  adapters/       a2l/dbc/ldf adapter + factory (extern "C" plugin entry points)
  ui/             memorygriditem, signalgriditem (QQuickPaintedItem renderers)
qml/
  Main.qml        root layout with SplitView, tabs, Loader
  components/     NavPanel, MemoryView, SignalMapView, Theme, Toast, SplashOverlay
docs/             design docs, screenshots
cmake/            FetchParserLib, DeployMsys2Deps
```

### Build

```bash
cmake -B build -G Ninja
cmake --build build
```

Qt 6.5+, CMake 3.21+, Protobuf required. Parser libraries are fetched automatically from GitHub releases at configure time.

### Code conventions

See workspace [CLAUDE.md "Code conventions"](../CLAUDE.md#code-conventions-workspace-single-source-of-truth). Repo-specific exceptions:

- Qt 6 + QML is the entire UI layer. The workspace "no Qt" rule applies to the parser/library repos, not here

`a2ldocumentsession.cpp` is ~2300 LOC and is queued for splitting along clear concern boundaries.

### CI / Release

- `ci.yml` runs on push to master: Windows MinGW + Ubuntu 24.04
- `release.yml` triggers on `v*` tags: builds Windows zip + Linux AppImage, publishes to GitHub Releases
- Do NOT re-tag unless the workflow is verified. Each release build takes ~3 min

### Platform differences

| | Windows | Linux |
|---|---------|-------|
| Backends | SHARED (.dll), loaded via QLibrary | STATIC, linked into exe, registered at startup |
| Qt deploy | Manual DLL copy from MSYS2 | AppImage via linuxdeploy |
| Protobuf JSON | `google/protobuf/util/json_util.h` (stable API, works on both v3 and v4+) | |
| Define | — | `BACKENDS_STATIC` |
