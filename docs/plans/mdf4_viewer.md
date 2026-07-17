# MDF4 data viewer — locked plan

Open an `.mf4` file in the explorer, browse its channel-group / channel hierarchy in the
tree, inspect channel metadata in the detail pane, and plot a selected channel (physical
value over time) in the center panel — zoom, pan, cursor readout.

Cross-repo plan: a new `mdf4-parser` repo (+ public `mdf4-parser-lib`) supplies the
reader; this repo grows the viewer backend. When `mdf4-parser` is scaffolded, its phases
move into that repo's `roadmap.md`; this file remains the viewer-side plan of record.

## Decisions (operator-approved)

- **D1 — reader home: new `mdf4-parser` repo + public `-lib`**, following the parser
  conventions. Keeps the reader's licensing independently decidable (dual-license like the
  other parsers, or not — operator sets `LICENSE.md` at repo creation; structure is
  identical either way).
- **D2 — v1 format subset: practical sorted subset.** ID/HD/DG/CG/CN/CC/TX/MD/SI blocks;
  numeric channels (uint/sint/float, byte- and bit-aligned, both endiannesses); stored
  (`cn_type` 2) and virtual (`cn_type` 3) time masters; identity/linear/rational/
  value-to-value conversions; DT blocks and DL block lists. Unsupported features degrade
  per channel/group with a diagnostic, never a failed load.
- **D3 — plot rendering: custom `QQuickPaintedItem`** (`SignalPlotItem`), following the
  `SignalGridItem` precedent. No new Qt modules, no new deploy surface.
- **D4 — load strategy: metadata-only at open, per-channel decode on demand** on a worker
  thread, cached per channel.
- **D5 — v1 plot scope: single channel** — click a channel in the tree, it plots.

## Interface split (consequence of D1 + D4)

The `-lib` surface is a hybrid — protobuf for the document, plain C++ for bulk samples:

- **Metadata as protobuf** (`mdf4` package, `lib/proto/mdf4/`): `mdf4::File` → channel
  groups → channels (name, source, unit, data type, bit geometry, conversion, sample
  count, master type, supported/unsupported + reason). Root carries
  `repeated Diagnostic diagnostics` per the parser diagnostics contract
  (`../../docs/ref/parser_diagnostics_contract.md` — extend that doc's scope to mdf4 when
  the repo lands). This keeps the explorer's detail cards, raw-JSON toggle, and
  diagnostics badge/popup working unchanged.
- **Samples as a direct C++ API** — bulk time-series data does not round-trip through
  protobuf.

```cpp
// lib/include/mdf4/extract.h        namespace mdf4::extract
mdf4::File extractFile(const std::string& path);          // block graph walk, no sample read
Series decodeChannel(const std::string& path,
                     uint32_t group, uint32_t channel);    // samples + time master, physical
// lib/include/mdf4/series.h
struct Series { std::vector<double> time; std::vector<double> value; };
```

Both calls are stateless (re-open by path) — no long-lived file handle, trivially usable
from worker threads. `extractFile` reads block headers only; sample count comes from CG
cycle counts, so open cost is proportional to structure, not file size.

## mdf4-parser repo

Mirror `dbc-parser`'s layout: root `CMakeLists.txt`, `lib/` submodule
(`include/mdf4/`, `proto/mdf4/`, `examples/`, its own `ci.yml`), `build.sh`, `README.md`,
`roadmap.md`, `project_status.md`, block-synced `CLAUDE.md`, `main` default branch,
`ci.yml` + `release.yml` (v* tags only) adapted from a sibling parser — reuse the vcpkg/
MSVC cache patterns, do not push to test CI. GitHub: private `dnbmch/mdf4-parser` +
public `dnbmch/mdf4-parser-lib`.

Reader internals (all C++17, no Qt, spec-derived — `mdf4-writer`'s block headers and the
BSD-2 legacy 4.1.0 reference serve as field-layout references; no code flows from the
proprietary repos unless the operator explicitly relicenses):

- `blocks` — 24-byte block-frame parse (id/length/link table), typed views for
  ID/HD/DG/CG/CN/CC/TX/MD/SI.
- `index` — walk the block graph once, produce the group/channel structure plus record
  layout and data-block ranges (DT or DL list) per group. This is the system boundary:
  malformed links/lengths/counts become diagnostics on a best-effort `File`, per the
  reporting-lenient contract.
- `decode` — stream a group's records, extract one channel + its time master, apply the
  conversion to physical doubles. Never materializes other channels.
- `extract` — the two public calls above; `extractFile` also serializes the proto.

Differences from the text parsers, stated up front: input is binary (no line numbers —
`Diagnostic.location` = block path + file offset), and the public API has the extra
`decodeChannel` / `Series` surface next to the proto document.

## Explorer backend

- **CMake**: `fetch_parser_lib(TARGET mdf4parser REPO dnbmch/mdf4-parser-lib VERSION
  v0.1.0 HEADER mdf4/extract.h)`; new `explorer-mdf4-backend` library block mirroring the
  existing three; exe wiring per platform. Add an mdf4 line to `seed-parser-deps.sh:41-43`
  so the explorer builds against the unreleased sibling working tree.
- **Dispatch**: `BackendSpec` entry for `.mf4` (`src/core/appcontroller.cpp:36-51`),
  `BACKENDS_STATIC` registration (`src/core/appcontroller.cpp:60-64`), `FileDialog` name
  filters (`qml/Main.qml:51-57`). `FormatId::MDF4` + display name already exist
  (`src/core/formatid.h:10-14`).
- **Adapter** `src/adapters/mdf4adapter.{h,cpp}`: `load()` = `extractFile`, map proto
  diagnostics to `DiagnosticMessage`s, construct session.
- **Session** `src/sessions/mdf4documentsession.{h,cpp}` (extends `AdapterSessionBase`):
  holds `mdf4::File _document`; tree = file → channel groups → channels (unit as
  subtitle); owns the decode cache and the async decode flow.
- **Presenter** `src/sessions/mdf4detailpresenter.{h,cpp}`: channel cards — data type,
  bit geometry, unit, conversion kind + coefficients, sample count, master type; group
  cards — record size, cycle count. Unsupported channels appear in tree + detail, marked
  not-plottable with the reason.

## Plot pipeline

- `src/models/signalplotmodel.{h,cpp}` — `QAbstractListModel` (center-panel contract,
  `src/sessions/documentsession.h:30`) holding the decoded `Series` + view state (visible
  time window, y-range, cursor) and the min/max-per-pixel bucket computation.
- `src/ui/signalplotitem.{h,cpp}` — `QQuickPaintedItem`: bucketed min/max column polyline
  when samples exceed ~2× pixel width, direct polyline otherwise; axes + tick labels;
  wheel zoom around cursor; drag pan; hover readout (time + value at nearest sample).
  Theme colors via `setColors()` like `SignalGridItem`. Registered in `main.cpp`.
- `qml/components/SignalPlotView.qml` — toolbar (channel name, unit, sample count,
  reset-zoom), plot item, status bar; returned by `centerPanelSource()`; registered in
  `qt_add_qml_module`.
- **Selection flow**: `selectNode(channel)` → session cache check → on miss,
  `decodeChannel` via `QtConcurrent` + `QFutureWatcher`, plot shows busy state, stale
  results (selection moved on) are dropped.

## Testing

- **mdf4-parser unit tests (ctest)**: fixtures are tiny `.mf4` files emitted by a test
  helper (byte-built at test time, no binary blobs in git — the ASAM spec-package files
  are not redistributable). Cover: block-graph walk, bit-aligned extraction both
  endiannesses, each conversion kind, stored + virtual masters, DL lists, malformed-file
  diagnostics (truncated block, bad link, zero-record group). Golden checks: extracted
  proto JSON vs golden, per parser convention.
- **Local ASAM corpus smoke** (exit-77 skip when absent): sweep
  `../a2l-parser/docs/ASAM_2022_04_07/` examples — index every file, decode one supported
  channel, assert no crash and correct unsupported-feature diagnostics.
- **asammdf cross-check** (guarded, exit-77 — pattern from `mdf4-writer`): decoded values
  match asammdf on selected fixtures.
- **Manual acceptance**: open an ASAM `Simple` example and an `mdf4-writer` output file in
  the explorer, plot a channel, verify values against `mdf4-writer/tools/mdf_roundtrip.py`.

## Phases + success criteria

1. **Scaffold `mdf4-parser` + `-lib`** (repos, submodule, CMake, CI adapted, CLAUDE.md
   block-synced). Done when: repo configures + empty-lib ctest runs; workspace `README.md`
   + `CLAUDE.md` tables (repo list, build table, conventions, branch note) include mdf4.
2. **Reader core** (`blocks`, `index`, `decode`, `extract`, proto). Done when: ctest green
   on byte-built fixtures; corpus smoke indexes the ASAM `Simple` + `ConversionLinear`
   families and decodes a channel with values matching asammdf.
3. **Explorer backend** (fetch + seed line, adapter, session, presenter, dispatch,
   CMake). Done when: opening an `.mf4` (seeded, unreleased parser) shows the channel tree
   + metadata cards; unsupported channels carry diagnostics; existing formats unaffected.
4. **Plot** (model, painted item, QML view, lazy decode wiring). Done when: clicking a
   channel plots it; zoom/pan/cursor work; a million-sample channel stays responsive;
   switching channels mid-decode doesn't race.
5. **Release + docs**: `mdf4-parser-lib` v0.1.0 (operator-timed — may ride the pending
   republish wave; explorer's mdf4 backend is not pushed before the release exists, since
   explorer CI must fetch it), `release.yml` backend-DLL entry, `EXPECTED_HASH` fill,
   updates to `README.md` / `roadmap.md` / `project_status.md` /
   `docs/arch/adapter_contract.md` / diagnostics-contract scope, backlog entries for the
   deferred items.

## Non-goals (v1 — candidate v1.1 items)

DZ-compressed data (first in line — Vector tools compress by default), invalidation
bits/bytes (v1 treats all samples valid), unsorted files, VLSD/MLSD string channels,
channel arrays, bus-logging composition, multi-channel overlay, export of plotted data,
TDMS (same seam, separate plan).
