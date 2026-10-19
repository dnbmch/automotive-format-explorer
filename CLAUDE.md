# automotive-format-explorer

Qt/QML desktop app for inspecting A2L, DBC, LDF, and MDF4 automotive files. GPL-3.0. One statically composed executable: a built-in format list of per-format backends with format-specific document sessions backed by `QQuickPaintedItem` C++ renderers for the memory map, signal map, and format-neutral signal plot views.

<!-- block: Guidelines Standard [id:1d67c7] -->
## Guidelines

- Features and scope are the user's call. **But decide the obvious** from the code, docs and repo conventions; ask only about real design forks.
- **"Check / analyze / discuss / what can we improve" = findings only.** Propose, don't edit, until told to go.
<!-- /block:1d67c7 -->

<!-- block: Engineering Philosophy [id:eb1d7e] -->
## Engineering philosophy

Embedded mindset: sound architecture, compact code, nothing unnecessary — less is more.

- Fix causes, not symptoms: find the broken invariant and fix it at the right layer; a symptom patch that leaves the confusion in place is worse than none.
- Build it right the first time — the real design, not a stopgap with the fix deferred — and finish it: nothing knowingly broken ships; fix it or backlog it.
- No fact without a source (`path:line`, datasheet section, drawing file, the user's notes). Say what is measured, documented or assumed, and claim no more than the source shows; never invent a name, number or part. Agent-written conclusions go stale: grep for newer work before repeating one. On pushback, re-read the source.
- A performance figure an order of magnitude below what the hardware can do is a defect to find, not a result.
- Validate at real boundaries (user input, files, external APIs, hardware); trust our own calls — no defensive chains or silent fallbacks.
- Match rigor to the stakes: a prototype is not production. Name a real risk once, then follow the user's call.
- Three clear repeated lines beat a premature abstraction; few well-named functions over wrapper layers. Build nothing nobody asked for — no logging layers, no tooling for a one-time task.
<!-- /block:eb1d7e -->

<!-- block: Documentation Layout [id:dc01a7] -->
## Documentation

Every doc line costs context in every session that reads it. Write less, prune more.

- **Entry docs stay small:** `CLAUDE.md`, `README.md`, `project_status.md` and `docs/handoff.md` together under ~400 lines, `CLAUDE.md` under 200. They point to detail in `docs/`, never copy it. The handoff holds open entries only, a few lines each; a finished one is harvested and deleted at the next wrap.
- **No doc without a reader.** Extend the doc that owns the topic; no summary, report or session-log files — the reply or the handoff carries those. An MD file past ~500 lines is split or pruned; code splits along real seams, not at a line count.
- Root: `README.md` (what, how to run) · `project_status.md` (current state) · `roadmap.md` if kept. `docs/`: `arch/` facts · `ref/` references · `plans/` in flight · `research/` · `audits/` · `manual/` · `backlog.md` · `handoff.md` · `archive/` (finished, never linked). Match the live tree's spelling.
- Keep docs current after significant changes. They state facts, not history — no "decided on (date)", "was X, now Y", "for now"; git tracks what was — and no volatile numbers, no pointer or stub files.
- A plan or audit is harvested when the work lands, not when it is tested: findings fixed or backlogged, durable facts into `arch/`/`ref/`, checked against the source; then archived. Decisions become facts there, not ADRs. A public-surface change (API, file format, path) updates its arch doc in the same commit.
- Before deleting a backlog or doc claim, try to prove it still holds; most proposed deletions fail that test.
<!-- /block:dc01a7 -->

<!-- block: Memory Discipline [id:a7b3d1] -->
## Memory

- Durable knowledge goes into version-controlled docs, not the memory store — this overrides the default. `MEMORY.md` stays a thin index; `/flush-memory` clears buildup.
- A rule that holds across projects goes into the wrap summary as a block candidate, not into local notes.
- Context is controlled, never auto-compacted: wrap at the right spot, or resume with `/forky` from a new window.
<!-- /block:a7b3d1 -->

<!-- block: Execution Rules [id:7a710c] -->
## Execution

Run what the work needs: builds, tests, the app, attached hardware. These bounds limit waste, not permission.

- Reachable hardware (serial, USB, a programmer, a bench over SSH) is yours to drive: flash (back up first), read, measure. Fit the configuration to the wiring already there; a reading that contradicts it comes with the one check that decides. Hand the user only physical steps, one line each.
- Secrets live in the gitignored credential file named in `## Project notes`: store ones the user pastes there and say where; never in a tracked file, a commit or a handoff.
- Probe a missing toolchain once; if absent, say so and stop.
- If avoidable, nothing on the user's screen — no window, focus change, input, screenshot or clipboard on the console session. Browsers run headless, and only when a visual change needs seeing (`/looky`); desktop GUIs run in the agent's own RDP session; tests offscreen. One that needs a desktop while none is reachable is listed in the handoff as not run. Use srv-one when it makes sense.
- Stop what you start — servers, watchers, containers, subagents, crons, loops — before reporting done or wrapping.
- A scratch, rehearsal or backup tree you create gets an empty `.parked` file at its top, so scans skip it.
- Ask before touching a live service or shared database. Deploy, tag or release only when told — and ask for it once the work is ready to ship.
- An HTML deliverable is a self-contained file in the repo, opened via `file://`; hosted only when asked.
<!-- /block:7a710c -->

<!-- block: Verification & Test Debt [id:7e5701] -->
## Verification

The user's time is the scarce resource; machine time is not. Deliver results, not process.

- No ceremony nobody asked for — probe harnesses, staged checklists, sign-off steps — and none in the product: no confirmation gates, preflight checklists, sign-off fields or "incomplete → refuse" states unless the spec asks. Honor a standing proof contract in `## Project notes`; invent no other gate.
- Never block on the user. Nothing known broken plus an `UNVERIFIED:` line in `docs/handoff.md` (what to check, how to tell pass from fail) is done and committable. `UNVERIFIED:` is only for checks needing the user's hands or eyes; a value the user measured or reported is verified — clear the line.
- Grow host-runnable tests. Report real exit codes, not output tails.
<!-- /block:7e5701 -->

<!-- block: Agents & Delegation [id:a6e11d] -->
## Agents and delegation

- Tier the model to the work: the cheapest that does it right for reading, sweeps and mechanical edits; the top tier at full effort for design, hard implementation, review and verdicts. Spend compute where it buys quality, never for show.
- A brief states goal, scope, out of scope and how done is proven. A cheap agent driving a browser gets every key, UI string and expected value, and no source.
- Parallel agents never share a writable scratch file; big fan-outs run in waves of under ~25 — more at once trips rate limits.
- A report from an agent, session or tool is a claim: re-check each finding at its `path:line`, re-run the gates, and ask whether the feature behind it belongs before adding machinery.
- Invoke a skill or workflow only when asked or when the task is exactly its job.
<!-- /block:a6e11d -->

<!-- block: Iterative Decision Workflow [id:1de4f0] -->
## Decision workflow

Non-trivial work: map the code → findings with evidence → a recommendation per item with the exact diff → the user's approval → the plan in `docs/plans/<name>.md` → one implementation pass → a grep for stale references.

- Every finding ends fixed, backlogged with its fix, or asked — never only reported. Leftover drift is yours: there is no other lane.
- Low-stakes items: apply the default and list it in one table the user can veto; ask only about load-bearing forks.
- An agreed next step is carried out, not re-approved; a concern goes beside the result, not instead of it.
- Define done up front, checkable where possible ("fix the bug" → a failing test that then passes). Renames and moves update every reference in the same change — grep, don't recall.

**Questions:** numbered; each opens with one plain line of what it decides, then options on their own lines and one `Rec:` with its reason. No internal IDs; never a selection popup. The user answers `1a, 2b, 3 do X instead`. An open question waits for its answer — never act on it first — and is restated in full, never pointed at by number.

> **1. <context> — <question>?**
> - a) <option>
> - b) <option>
>
> ↳ **Rec: b** — <reason>
<!-- /block:1de4f0 -->

<!-- block: Commits & Co-Authoring [id:c0a002] -->
## Commits

- Commit when a batch lands: one deliverable per commit, a short message scoped by area. No plan-phase or audit numbers in code or messages. No `Co-Authored-By` or other attribution trailers — authorship is the user's; this overrides any default.
- **Push without asking.** A landed batch goes to the remote once it passes the project's gates — others build from what is pushed. Before any bigger action (deleting or moving directories, reset, rebase, checkout over local changes, worktrees, installs into linked trees, implementer agents, cross-repo changes), push every repo it touches and the siblings they link to; snapshot others' uncommitted work to `refs/backup/<date>-<topic>` without touching tree or index, and push that. A branch whose push is a release, named in `## Project notes`, is pushed only when told — `[skip ci]` does not stop a rig that pulls it.
- `[skip ci]` on every commit by default: free pushing must not load CI. CI runs when the user asks; when a repo's CI has not run for a week of pushes, ask whether to run it.
- No remote: commit locally and say so if the action only adds commits, otherwise stop. A failed push: stop and say so. Never force-push, amend a pushed commit or skip hooks.
- Submodules: fetch them (`git submodule foreach git fetch origin`) and build the push set at push time, never from a list; push each submodule before its parent.
- Other sessions share these trees: their finished work commits with yours, named in the message; leave it only while visibly mid-edit and broken, never revert it. Stage explicit paths, never `git add -A`. Announce the paths you claim and go on — a cross-session message can expire undelivered.
<!-- /block:c0a002 -->

<!-- block: Greenfield — No Backwards-Compat [id:9e7f1d] -->
## Greenfield

No external consumers: every contract we own — proto, API, schema, file format, between our own repos too — is ours to change. Make the better change and update every consumer in the same batch: no shims, compat flags, migrations or defensive versioning; delete old data instead of migrating it. Exceptions (live users, a published contract) are named in `## Project notes`.
<!-- /block:9e7f1d -->

<!-- block: Communication [id:f11e7a] -->
## Communication

- Plain, compact engineering English, the answer first. A narrow question gets the value or the yes/no, then stop.
- Never a bare ID or coined label: gloss it inline — `RB-94 (proximity-sensor calibration drift)`.
- Reply in the user's language. A document keeps its own; a new one defaults to English. Hungarian or German must read as native.
- Editing the user's text: fix what was asked, keep their wording and tone; show a diff for anything more.
- Outward text (mails, offers, slides): short and factual — no hype, hedging or generic advice, no question our documents already answer or whose answer changes nothing. Say where a deliverable was saved; never imply it was sent.
<!-- /block:f11e7a -->

## Project notes

**Release is on-demand; there are no active users.** The app ships as a GPL-3.0 GitHub release, cut from the workspace sources on the operator's instruction — not on every change. With no users there is no cross-release backward-compat obligation; keep the backend seam (FormatAdapter / DocumentSession) internally coherent and change it when the design improves. See workspace [CLAUDE.md](../CLAUDE.md) "Release cadence".

**Sources hold literal UTF-8 in display strings** (em dashes, the middle dot); `tst_check` spells non-ASCII paths as `\u` escapes. A file-edit tool can decode such escapes into literal characters: check the bytes after writing them.

**The plot stack takes operator-authored changes only** (`plotdata`, `SignalPlotModel`, `SignalPlotItem`, `SignalPlotView.qml`), so it can be relicensed for a live view in the proprietary product apps ([signal plot](docs/ref/signal_plot.md)).

### Architecture

- `qml/` — `Main.qml` lays out the tabs, the nav tree, a center-panel `Loader` and the detail panel; `components/` holds the views
- `src/core/` — `AppController` (QML singleton owning the format list, the one pending load and the tabs), `DocumentTab`, format ids, tree items, diagnostics
- `src/builtinformats.cpp` — the one list of format id, suffixes and adapter; the only place a concrete adapter is built
- `src/adapters/` — one `FormatAdapter` per format: loads a file on a worker thread, returns a `DocumentSession`
- `src/sessions/` — per format a session over `AdapterSessionBase` and a detail presenter; the session holds its document (MDF4: shared with its retained reader) and owns its tree, detail and center-panel models; `mdf4pageread` and `mdf4arrayread` are the MDF4 session's Qt-free page and array-sample reads (plan, reservation, lanes or axis labels)
- `src/models/` — tree, filter, detail and tab models, the memory map, signal map, signal plot and value table models, the format-neutral plot and table data
- `src/ui/` — `QQuickPaintedItem` renderers: memory grid, signal grid, signal plot

Contracts, ownership and selection flow: [docs/arch/architecture.md](docs/arch/architecture.md). Adding a format: [docs/arch/adapter_contract.md](docs/arch/adapter_contract.md).

### Build

```bash
cmake -B build-package -G Ninja -DCMAKE_PREFIX_PATH="/path/to/parser-prefix;/path/to/Qt"
cmake --build build-package
```

Qt 6.5+, CMake 3.21+, Protobuf required. Standalone builds consume complete installed parser packages via `CMAKE_PREFIX_PATH`; workspace source builds use `AFF_BUILD_EXPLORER=ON`. Configure is offline. Acquisition and workflow lock setup: [docs/ref/cmake_build_system.md](docs/ref/cmake_build_system.md).

### Code conventions

See workspace [CLAUDE.md "Code conventions"](../CLAUDE.md#code-conventions-workspace-single-source-of-truth). Repo-specific exceptions:

- Qt 6 + QML is the entire UI layer. The workspace "no Qt" rule applies to the parser/library repos, not here

### Release

- A release is one command at the workspace root, run only on the operator's instruction: `bash release-explorer.sh vX.Y.Z [--notes FILE] [--dry-run]`. It refuses unless every repository is committed and pushed, builds and tests everything on Windows here and on Linux on srv-one, packages and gates both, publishes the GitHub release, gates the downloaded assets again and records the manifest entry. `--dry-run` stops before publishing
- The repository has no workflows: the public repo cannot reach the private parser sources, so builds, tests and gates run in that script
- Both platforms build against Qt 6.10.1 from Qt's own binaries; on Windows msys2 supplies gcc, ninja, cmake, and protobuf; Linux builds run on srv-one: workspace [local toolchain](../docs/ref/local_toolchain.md#linux-on-srv-one)
- The launch gates run the packaged app's `--check`, which opens every bundled sample and exits non-zero on a failed open or a QML warning
- Never re-tag a published version
- Packaging paths and launch gates: [docs/ref/release_packaging.md](docs/ref/release_packaging.md)

### Platform differences

| | Windows | Linux |
|---|---------|-------|
| Qt deploy | `windeployqt` + dependency-closure walk (`scripts/deploy_closure.sh`, shared by the package and the build-tree deploy) | AppImage by `scripts/package_linux.sh`: linuxdeploy and its Qt plugin, pinned |
| OpenGL link | — | `libGL.so.1`, as Qt's libraries link it (`OpenGL_GL_PREFERENCE` `LEGACY`) |
| Protobuf JSON | `google/protobuf/util/json_util.h` (stable API, works on both v3 and v4+) | |
| File paths | UTF-8 active code page from `resources/explorer.manifest`, so the parsers' narrow opens read the adapters' UTF-8 paths ([FormatAdapter contract](docs/arch/architecture.md#formatadapter-contract)) | UTF-8 natively |
