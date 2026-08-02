# Backlog — automotive-format-explorer

Items deliberately deferred or pending. Each entry says **what**, **where**, and the rough size.

## Parked operator decisions

Structural findings from /enforce coverage, awaiting a call — deliberately not applied.

### BL-E2: adapter repetition

The four adapters repeat format identity, factory, and protobuf-diagnostic
translation. Their actual load boundaries differ: A2L/DBC/LDF own text-loader
objects while MDF4 extracts metadata directly and defers bulk samples to its
session. A generic load template would hide that distinction for little source
reduction. Keep the explicit adapters; extract only diagnostic translation if
its semantics ever need to change together.

**Size:** S for the diagnostic helper; the explicit load flows are a deliberate keep.

## Signal plot / MDF4

### BL-P1: decoded sample memory is unbounded

`Mdf4DocumentSession::selectChannel` always asks for the whole channel
(`src/sessions/mdf4documentsession.cpp:264` passes `firstSample = 0`,
`sampleCount = channel.sample_count()`), and `_decode_cache`
(`src/sessions/mdf4documentsession.h:43`) never evicts. Every channel a user
clicks stays resident at 16 bytes/sample for the life of the tab, and the
`emplace` at `src/sessions/mdf4documentsession.cpp:257` copies the series
before the model move, so peak is briefly double. Browsing a recording with
many long channels — say 100 channels of 1M samples — retains ~1.6 GB with no
back pressure. The locked plan reserved `decodeChannel`'s sample window for
exactly this ("the sample window caps it for huge channels",
`docs/plans/mdf4_viewer.md`), but no cap is applied. Needs an operator call on
the policy: decode a bounded window and refine on zoom, cap the cache by total
samples with LRU eviction, or leave it unbounded for the recording sizes we
actually demo.

**Size:** S for an LRU/byte-budget cache; M if the sample window becomes a real
viewport-driven fetch.

### BL-P2: a superseded decode result is thrown away instead of cached

The completion handler returns before the cache write when the generation token
has moved on (`src/sessions/mdf4documentsession.cpp:249-257`), so a decode that
finished correctly is discarded along with the work that produced it. Clicking
A → B → A re-decodes A from scratch, and the second visit can run a duplicate
decode of A while the first is still in flight. The result is valid data for its
channel regardless of what is selected now; caching it before the generation
check would keep the model update guarded while retaining the work. Cost is
bounded by whatever BL-P1 settles on for cache size.

**Size:** XS — move the `emplace` above the generation check.

### BL-P3: plot correctness depends on an unvalidated monotonic domain

`SignalPlotModel` binary-searches the domain vector in `visibleSampleRange` and
`setCursorTime` (`src/models/signalplotmodel.cpp:201`, `:234-235`), so a
non-monotonic time master silently yields a wrong visible range, wrong bucket
boundaries, and a cursor that snaps to the wrong sample — no crash, no
diagnostic. `docs/ref/signal_plot.md` states the assumption and today's
producer holds it (the reader only accepts `sync_type == 1` masters and
otherwise falls back to record indices), but unsorted MDF4 files are an
explicitly deferred reader increment, so the guarantee weakens as reader
breadth grows. Decide whether the session validates monotonicity at the seam
(it is a parsed-file boundary) or the plot degrades to index-domain when the
check fails.

**Size:** S.

### BL-P4: the time master is offered as a plottable channel

`buildTree` marks every `decodable()` channel as a plottable entity
(`src/sessions/mdf4documentsession.cpp:161-176`), including the group's own time
master. Selecting it plots the master against itself — a 45° line — because
`decode::channel` returns `series.time = series.value` when the target is the
master. Harmless but meaningless; the tree could mark masters as axis channels
instead of offering them as signals.

**Size:** XS.

### BL-P5: the writer-file smoke reports green without running

`tst_mdf4documentsession::siblingWriterFileOpensAndPlots`
(`tests/tst_mdf4documentsession.cpp:170-175`) `QSKIP`s unless
`MDF4_WRITER_SAMPLE` names an `.mf4`, and QTest counts a skipped case as a pass,
so a plain `ctest --test-dir build` shows 6/6 green while the only end-to-end
"a real recording opens and plots" case never executed. The gate itself is right
— no binary fixture belongs in git — but the signal is misleading. The reader
repo solved the same problem with an exit-77 ctest skip
(`docs/plans/mdf4_viewer.md`, corpus smoke), which surfaces as `Skipped` rather
than `Passed`. Options: adopt exit-77 here, or have the test generate its own
`.mf4` from the sibling writer at build time so it always runs.

Run it by hand meanwhile:
`MDF4_WRITER_SAMPLE=<file>.mf4 ./build/tst_mdf4documentsession.exe`

**Size:** XS for exit-77; S to generate a fixture at test time.
