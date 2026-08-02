# MDF4 data viewer — locked plan

Open an `.mf4` file in the explorer, browse its channel-group / channel hierarchy in the
tree, inspect channel metadata in the detail pane, and plot a selected channel (physical
value over time) in the center panel — zoom, pan, cursor readout.

Cross-repo plan: a new `mdf4-parser` repo (+ public `mdf4-parser-lib`) supplies the
reader and doubles as `mdf4-writer`'s independent verification gate; this repo grows the
viewer backend and a format-agnostic signal plot module. When `mdf4-parser` is
scaffolded, its phases move into that repo's `roadmap.md`; this file remains the
viewer-side plan of record.

## Decisions (operator-approved)

- **Reader home: new `mdf4-parser` repo + public `-lib`**, parser conventions, with a
  second duty: `mdf4-writer`'s round-trip tests consume the sibling reader as an
  **independent verification gate**. Independence is the point — the reader shares no
  code or block structs with the writer, so it cannot inherit the writer's layout
  assumptions. Licensing stays independently decidable (operator sets `LICENSE.md` at
  repo creation).
- **Reader scope: lockstep with the writer.** The reader reads what `mdf4-writer` emits
  and grows when the writer grows (M6 row DL/HL/DZ, MLSD, …). Stored-time masters are
  also in the read set because the foreign-writer cross-gate requires ordinary
  asammdf output. Other foreign-file
  breadth beyond that is a set of priced, additive increments (see Deferred increments) —
  spent when a concrete showcase moment exists, not before. Any unsupported feature
  degrades per channel/group with a diagnostic, never a failed load: foreign files still
  open and show their full structure; unsupported channels are visible but not plottable.
- **Signal display is format-agnostic.** The plot module (model + painted item + QML)
  never sees format types; sessions convert into an explorer-local series type at the
  seam. A future TDMS backend — or a lift into the product apps for live view — costs a
  reader + session only, zero plot work.
- **Plot rendering: custom `QQuickPaintedItem`** (`SignalPlotItem`), following the
  `SignalGridItem` precedent. No new Qt modules, no new deploy surface.
- **Load strategy: metadata-only at open, per-channel decode on demand** on a worker
  thread, cached per channel.
- **v1 plot scope: single channel** — click a channel in the tree, it plots.

## v1 reader scope (writer output + foreign-writer gate)

ID/HD/FH/DG/CG/CN/CC/TX/MD/SI blocks; row (`##DT` or a direct `##DZ` replacing
DT) **and column (`##DV` or `##LD`, including `##DZ`-compressed fragments)**
storage layouts; virtual equidistant (`cn_type` 3) and stored (`cn_type` 2) time
masters, local or remote; little-endian uint/sint channels at 8/16/32/64 bits
and IEEE float channels at 32/64 bits; the
writer's full conversion set — identity, linear, rational, value-to-value tables with
and without interpolation, and value-to-text (decoded as raw numerics with the labels
in the metadata document). Column/DZ are in scope because the writer emits them;
the foreign-writer gate also requires direct row DZ and stored axes. The optional
asammdf reader leg retains a documented LDBLOCK bug workaround
(`../mdf4-writer/docs/backlog.md`).

## Interface split

The `-lib` surface is a hybrid — protobuf for the document, plain C++ for bulk samples:

- **Metadata as protobuf** (`mdf4` package, `lib/proto/mdf4/`): `mdf4::File` → channel
  groups → channels (name, source, unit, data type, bit geometry, conversion, sample
  count, master type, supported/unsupported + reason). Root carries
  `repeated Diagnostic diagnostics` per the parser diagnostics contract
  (`../../docs/ref/parser_diagnostics_contract.md`, which includes MDF4). This
  keeps the explorer's detail cards, raw-JSON toggle, and
  diagnostics badge/popup working unchanged.
- **Samples as a direct C++ API** — bulk time-series data does not round-trip through
  protobuf.

```cpp
// lib/include/mdf4/extract.h        namespace mdf4::extract
mdf4::File extractFile(const std::string& path);          // block graph walk, no sample read
Series decodeChannel(const std::string& path,
                     uint32_t group, uint32_t channel,     // samples + time master, physical
                     uint64_t firstSample = 0,
                     uint64_t sampleCount = UINT64_MAX);   // sample window, clamped
// lib/include/mdf4/series.h
struct Series { std::vector<double> time; std::vector<double> value; };
```

Both calls are stateless (re-open by path) — no long-lived file handle, trivially usable
from worker threads. `extractFile` reads block headers only; sample count comes from CG
cycle counts, so open cost is proportional to structure, not file size. Decode streams
through a bounded buffer, so file size never bounds RAM; the decoded `Series`
(16 bytes/sample) is the only output-size cost, and the sample window caps it for
huge channels.

## mdf4-parser repo

Mirror `dbc-parser`'s layout: root `CMakeLists.txt`, `lib/` submodule
(`include/mdf4/`, `proto/mdf4/`, `examples/`, its own `ci.yml`), `build.sh`, `README.md`,
`roadmap.md`, `project_status.md`, block-synced `CLAUDE.md`, `main` default branch,
`ci.yml` + `release.yml` (v* tags only) adapted from a sibling parser — reuse the vcpkg/
MSVC cache patterns, do not push to test CI. GitHub: private `dnbmch/mdf4-parser` +
public `dnbmch/mdf4-parser-lib`.

Reader internals (all C++17, no Qt, implemented **from the ASAM spec only** — no code or
struct sharing with `mdf4-writer`, since independence is what makes the verification gate
worth having):

- `blocks` — 24-byte block-frame parse (id/length/link table), typed views for the v1
  block set.
- `index` — walk the block graph once, produce the group/channel structure plus record
  layout and data-block ranges (DT/DZ or LD/DV/DZ chain) per group. This is the system
  boundary: malformed links/lengths/counts become diagnostics on a best-effort `File`,
  per the reporting-lenient contract.
- `decode` — stream a group's records (row) or value blocks (column), extract one channel
  + its time master, apply the conversion to physical doubles. Never materializes other
  channels.
- `extract` — the two public calls above; `extractFile` also serializes the proto.

Differences from the text parsers, stated up front: input is binary (no line numbers —
`Diagnostic.location` = block path + file offset), and the public API has the extra
`decodeChannel` / `Series` surface next to the proto document.

## Writer verification gate (in mdf4-writer)

The round-trip CTest in `mdf4-writer` consumes the **sibling** `mdf4-parser` working
tree (the same sibling-consumption pattern it uses for `signal-core`). It writes
every fixture-catalog `Recording` in Row, Column, and ColumnCompressed, then reads
physical values and time axes through `mdf4::extract` and compares them with the
catalog. A large mixed DV/DZ-under-LD case keeps the compressed path non-vacuous.
Guarded cross-gates compare mdf4-parser and asammdf on the same writer file, then
reverse the producer direction by decoding plain/compressed files written by
asammdf at test time. License direction is clean — the proprietary writer consumes
the open parser for verification, never the reverse.

## Explorer backend

- **CMake**: `fetch_parser_lib(TARGET mdf4parser REPO dnbmch/mdf4-parser-lib VERSION
  v0.1.0 HEADER mdf4/extract.h)`; new `explorer-mdf4-backend` library block mirroring the
  existing three; exe wiring per platform. Add an mdf4 line to `seed-parser-deps.sh:41-43`
  so the explorer builds against the unreleased sibling working tree.
- **Dispatch**: `BackendSpec` entry for `.mf4` (`src/core/appcontroller.cpp:36-51`),
  `BACKENDS_STATIC` registration (`src/core/appcontroller.cpp:60-64`), `FileDialog` name
  filters (`qml/Main.qml:51-57`). `FormatId::MDF4` + display name already exist
  (`src/core/formatid.h:10-14`) — reserved for exactly this read-back use.
- **Adapter** `src/adapters/mdf4adapter.{h,cpp}`: `load()` = `extractFile`, map proto
  diagnostics to `DiagnosticMessage`s, construct session.
- **Session** `src/sessions/mdf4documentsession.{h,cpp}` (extends `AdapterSessionBase`):
  holds `mdf4::File _document`; tree = file → channel groups → channels (unit as
  subtitle); owns the decode cache and the async decode flow; converts decoded data into
  the plot module's series type at this seam.
- **Presenter** `src/sessions/mdf4detailpresenter.{h,cpp}`: channel cards — data type,
  bit geometry, unit, conversion kind + coefficients, sample count, master type; group
  cards — record size, cycle count, storage layout. Unsupported channels appear in tree +
  detail, marked not-plottable with the reason.

## Plot module (format-agnostic)

The seam is an explorer-local value type — sessions produce it, the plot stack consumes
only it, and no `mdf4::` (or future format) type crosses the line:

```cpp
// src/models/plotseries.h
struct PlotSeries {
    QString name, unit;
    QString domainName, domainUnit;
    std::vector<double> time, value;
};
```

- `src/models/signalplotmodel.{h,cpp}` — `QAbstractListModel` (center-panel contract,
  `src/sessions/documentsession.h:30`) holding the current `PlotSeries` + view state
  (visible time window, y-range, cursor, busy flag) and the min/max-per-pixel bucket
  computation. API surface: `setSeries(PlotSeries)`, `setBusy(bool)` — that is the whole
  provider contract for v1; a formal interface class waits until a second producer
  exists.
- `src/ui/signalplotitem.{h,cpp}` — `QQuickPaintedItem`: bucketed min/max column polyline
  when samples exceed ~2× pixel width, direct polyline otherwise; axes + tick labels;
  wheel zoom around cursor; drag pan; hover readout (time + value at nearest sample).
  Theme colors via `setColors()` like `SignalGridItem`. Registered in `main.cpp`.
- `qml/components/SignalPlotView.qml` — toolbar (signal name, unit, sample count,
  reset-zoom), plot item, status bar; returned by `centerPanelSource()`; registered in
  `qt_add_qml_module`.
- **Selection flow**: `selectNode(channel)` → session cache check → on miss,
  `decodeChannel` via `QtConcurrent` + `QFutureWatcher`, plot shows busy state, stale
  results (selection moved on) are dropped.
- **Reuse note**: the plot module is a candidate for later lift into the proprietary apps
  (live view off the UDP feed). Keep it contribution-clean — operator-authored only — so
  self-relicensing stays possible.

## Testing

- **mdf4-parser unit tests (ctest)**: fixtures are tiny `.mf4` files emitted by a test
  helper (byte-built at test time, no binary blobs in git — the ASAM spec-package files
  are not redistributable). Cover: block-graph walk, row and column storage, virtual
  and stored masters (including a remote compressed-column axis), each writer-set
  conversion, malformed-file diagnostics (truncated block, bad link, zero-record
  group), and unsupported-feature degradation. Assertions inspect typed proto
  metadata and decoded series directly; there is no committed binary or JSON golden.
- **Local ASAM corpus smoke** (exit-77 skip when absent): sweep
  `../a2l-parser/docs/ASAM_2022_04_07/` examples — index every file without crashing,
  check version metadata for each file, and require at least one file to yield groups.
  Diagnostic counts are reported for visibility, not asserted per file; decoding is
  not expected from this smoke.
- **Writer round-trip gate**: the mdf4-writer-side ctest described above — this is the
  primary value-correctness check for the writer-output portion; the reverse
  asammdf-writer gate covers v1's stored-master and direct-row-DZ additions.
- **Manual acceptance**: open an `mdf4-writer` output file in the explorer, plot a
  channel, verify values against `mdf4-writer/tools/mdf_roundtrip.py`; open an ASAM
  example and confirm graceful structure-only display.

## Phases + success criteria

1. **Scaffold `mdf4-parser` + `-lib`** (repos, submodule, CMake, CI adapted, CLAUDE.md
   block-synced). Done when: repo configures + empty-lib ctest runs; workspace `README.md`
   + `CLAUDE.md` tables (repo list, build table, conventions, branch note) include mdf4.
2. **Reader core** (`blocks`, `index`, `decode`, `extract`, proto — v1 scope incl.
   column). Done when: ctest green on byte-built fixtures across the owned layouts;
   ASAM corpus smoke indexes every file gracefully, checks versions, and observes
   structure in the corpus.
3. **Writer verification gate — complete** (in `mdf4-writer`). Row, Column, and
   ColumnCompressed catalog round-trips are green against the sibling parser with
   physical values and time axes matching ground truth; guarded asammdf reader and
   writer cross-gates cover the foreign implementation boundary. The complete
   matrix lives in `mdf4-writer/docs/arch/verification.md`.
4. **Explorer backend — complete** (fetch + seed line, adapter, session, presenter, dispatch,
   CMake). Done when: opening an `.mf4` (seeded, unreleased parser) shows the channel
   tree + metadata cards; unsupported channels carry diagnostics; existing formats
   unaffected.
5. **Plot module — complete** (plotseries, model, painted item, QML view, lazy decode wiring). Done
   when: clicking a channel plots it; zoom/pan/cursor work; a million-sample channel
   stays responsive; switching channels mid-decode doesn't race; no format types in the
   plot module (grep-checkable).
6. **Release + docs**: `mdf4-parser-lib` v0.1.0 (operator-timed — may ride the pending
   republish wave; explorer's mdf4 backend is not pushed before the release exists, since
   explorer CI must fetch it), `release.yml` backend-DLL entry, `EXPECTED_HASH` fill,
   updates to `README.md` / `roadmap.md` / `project_status.md` /
   `docs/arch/adapter_contract.md` / diagnostics-contract scope, backlog entries for the
   deferred increments.

## Deferred increments (additive; spend when a showcase moment exists)

Reader breadth, roughly in demo-value order: big-endian + arbitrary bit-aligned channels;
remaining conversion families (algebraic `cc_type` 3, value-range 6/8, text-keyed
9–11); DL row-storage lists (enters lockstep scope anyway when the writer's M6
lands); invalidation bits; unsorted files; VLSD/MLSD string channels; channel arrays;
bus-logging composition.

Viewer: multi-channel overlay, export of plotted data, TDMS backend (same plot module,
new reader + session), live view (product-side lift).
