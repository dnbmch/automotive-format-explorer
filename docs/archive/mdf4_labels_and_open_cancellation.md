# MDF4 detail labels and opening cancellation

Two bounded Explorer batches, both at the presentation and loading boundaries.
No new scheduler; the reader is not changed.

## 1. Detail labels (BL-E3)

Code: `src/sessions/mdf4detailpresenter.cpp`.

- `dataTypeText`: add `mdf4::UINT_BE` → "Unsigned integer (big-endian)",
  `mdf4::SINT_BE` → "Signed integer (big-endian)", `mdf4::FLOAT_BE` →
  "IEEE floating point (big-endian)". The `"Unknown (%1)"` fallback stays for
  values the proto does not name.
- `conversionKindText`: add `mdf4::ALGEBRAIC` → "Algebraic formula",
  `mdf4::TAB_RANGE` → "Value range table".
- Conversion section: add the field "Formula" carrying `Conversion.formula()`;
  `addField` already drops it when empty.

Test: `tests/tst_mdf4detailpresenter.cpp`, one new case built the way
`makeDocument()` builds its input: a channel with `FLOAT_BE` and an `ALGEBRAIC`
conversion carrying formula text asserts the "Data Type", "Kind" and "Formula"
field values; a channel with `SINT_BE` and `TAB_RANGE` asserts its two labels.

Docs: remove BL-E3 from `docs/backlog.md`. No arch or ref doc lists label strings.

## 2. Cancel an MDF4 opening during shutdown (BL-V1)

Contract change in `src/adapters/formatadapter.h`:

```cpp
virtual LoadResult load(const QString& path, const std::atomic<bool>& cancel) const = 0;
```

Every adapter and every caller changes in the same batch: no default argument,
no overload.

- `src/adapters/mdf4adapter.cpp`: construct the reader with the flag, passing the
  reader's default limits explicitly and `&cancel` as its third argument (the
  constructor is `Reader(const std::string&, const Limits& = Limits{}, const
  std::atomic<bool>* = nullptr)` in `mdf4/reader.h`; check the exact `Limits`
  type name). Update the comment above `load`. The rest is unchanged: a cancelled
  opening still yields a session whose diagnostics carry the reader's DROPPED
  "opening cancelled" entry.
- A2L, DBC and LDF adapters take the parameter unnamed; their parsers have no
  cancel API.
- `src/core/appcontroller.h` and `.cpp`: a member `std::atomic<bool> _load_cancel`.
  `openFile` stores `false` before launching, and the worker lambda captures a
  pointer to the member (the controller outlives the worker: `shutdown()` joins
  it and the destructor calls `shutdown()`). `shutdown()` stores `true` right
  before `pending.waitForFinished()`. The `shutdown()` contract comment in the
  header says it asks a pending load to stop; an MDF4 opening observes the
  request, a text parse returns on its own.

Tests:

- `tests/tst_appcontroller.cpp`: `FakeAdapter::load` takes the new parameter. A
  `Probe` toggle `honorsCancel` (default false, so every existing case keeps its
  gate semantics, like a text adapter); when set, the blocked load returns as soon
  as the flag is set and logs "load cancelled". New case
  `shutdownCancelsRunningLoad`: open a file with `honorsCancel` set, never open
  the gate, call `shutdown()`; expect it to return inside the existing deadlock
  guard, the log to show "load cancelled" before "session destroyed", and no
  `fileLoaded`.
- `tests/tst_mdf4writerfile.cpp` (or `tst_mdf4recording.cpp`, whichever already
  opens a real `.mf4` through `Mdf4Adapter`): new case
  `cancelledOpeningYieldsDroppedDiagnostic`: a `std::atomic<bool>` already true,
  `adapter.load(path, cancel)`; expect a session and a diagnostic whose detail
  contains "opening cancelled". This proves the flag reaches the reader.
- Every other `load(` call site in tests passes a `std::atomic<bool>` that stays
  false.

Docs in the same batch: `docs/arch/architecture.md` (the "parse is not
interruptible" paragraph and the MDF4 "no cancellation" sentence),
`docs/arch/adapter_contract.md` (shutdown asks a pending load to stop and waits
for it), `project_status.md` (the `AppController` line), remove BL-V1 from
`docs/backlog.md`. The handoff's UNVERIFIED line about closing during a large
A2L load still holds and stays.

## Proof

Build the configured `build-consolidation/` tree (PACKAGE mode against
`build-j/speed/gates/parser-prefix`; PATH per `build.sh`), then:

```
ctest --test-dir build-consolidation -R 'tst_appcontroller|tst_mdf4detailpresenter|tst_mdf4writerfile|tst_mdf4recording|tst_builtinformats' --output-on-failure
```

and once at the end the full `ctest --test-dir build-consolidation
--output-on-failure`. Done: both new lifecycle cases and the label case pass,
every suite green, no new warnings in the touched files, and no live doc still
describes the load as not interruptible for MDF4.
