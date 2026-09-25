# automotive-format-explorer — handoff

## 2026-09-25 — sparse memory view (workspace cleanup batch H) — OPEN

Committed locally as "memory view: resolve bytes from sorted object intervals", after
the accepted G1 (`daec46e`) and I1 (`325267e`) commits; parent review is pending, and
corrections land as follow-up commits. `MemoryMapModel` keeps the current segment's
objects as sorted intervals with a running maximum of their ends, and
`queryBytes(start, count)` resolves ownership and overlap for any byte range from them:
the row drawn last (highest start address, then document order) owns a shared byte.
`objectAtAddress` and `isOverlap` are that query for one byte. `MemoryGridItem` paints
the visible rows from one tile, hit-tests through the same query, and gives each object
its shade once per segment in address order. The 16 MiB overlap map, color map and
object map and `clampedByteSpan` are gone. Row geometry (`totalRows`, `rowForAddress`,
`contentHeight`, row positions) is 64-bit; object and segment ends saturate at the top
of the address space; objects at one address keep document order. Contract:
[memory view](ref/memory_view.md), [rendering](arch/architecture.md#rendering).

Verified: standalone PACKAGE build against `build-i1-parser-prefix` 10/10, no
warnings; `tst_memorymapmodel` 13 cases, among them a per-byte oracle over 300 random
layouts with straddlers, shared starts and unknown sizes; new `tst_memorygriditem`
3 cases painting offscreen into an image, including clicks; strict
`-Wshadow -Wconversion -Wsign-conversion` replay clean on the four touched sources, as
the two production files were before; five repeated runs of both memory suites clean.
Workspace SOURCE build 232/232. Before the change, `tst_memorygriditem` fails on the
pre-H model and grid: an object 48 MiB into a 64 MiB segment paints unoccupied, and a
3 GiB derived segment's `contentHeight` overflows to −469,761,744. Five mutants each
fail their case: unstable sort, first row wins, no segment clip, wrapping interval
ends, shades assigned per painted range. Evidence: `build-h/` at the workspace root.
G1 and I1 evidence: the [DBC/Explorer](../../docs/audit/dbc_explorer_review.md) and
[Reader](../../docs/audit/mdf4_reader_review.md) reviews.

Landmines:
- `tst_memorygriditem` compiles `src/ui/memorygriditem.cpp` itself (the painted items
  are part of the executable, not a library) and runs with `QT_QPA_PLATFORM=offscreen`.
  Pixel checks compare 8-bit RGB; a `QColor` keeps finer components than the image stores.
- `tst_appcontroller` and `tst_mdf4documentsession` (`settle()`) synchronise on
  `QThreadPool::globalInstance()->waitForDone()`; a test that leaves unrelated pool work
  running, or blocks a read, would make that wait cover it too.
- Closing a session from inside the plot model's reset or series notifications is
  unsupported (a Qt model cannot be destroyed while emitting); a read completion's last
  notification is the busy change.
- Runtime provenance is load-bearing in the ctest `PATH`, the packaging closure order
  and `--search`-before-`--provided` ([build reference](ref/cmake_build_system.md)); any
  script CMake or Ninja invokes must pin its own msys runtime's tools first. On Windows,
  `qt_standard_project_setup()` emits every executable, tests included, into the build
  root.

UNVERIFIED — no CI has run: Linux and macOS builds of the static composition, the MDF4
session and the sparse memory view; the Linux AppImage packaging and launch gate; a
headless Windows launch of the package (BL-K6: the gate can pass on a fatal-error dialog).

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
- An A2L whose objects span more than 16 MiB, for example RAM and flash without
  `MEMORY_SEGMENT`s: objects at both ends of the derived segment paint in their colors;
  scrolling or "Go to" reaches a far address and flash-highlights its object; clicking
  an object selects it in the tree; overlaps far into the segment are hatched.

Fail = missing or unfiltered entries, a dead sample link, clipped tabs, an empty
`speed` plot or an empty plot without explanation, a stale channel shown last, a
stuck busy veil, a crash, a hang outlasting the parse or read, an unoccupied cell where
an object belongs, a scrollbar that cannot reach the far end, or a click that selects
nothing.

Open: the operator's verdict on a real-world `.mf4`. If one comes back mostly
non-plottable and matters, dump it with the parser's `mdf4_json`, map each
non-decodable channel class to the reader increment that unlocks it (VLSD,
unsorted, arrays, MLSD, bus logging) and spec those increments as a locked plan;
reader breadth is bought, not assumed.

Next: G2 after H's review — kickoff in the [workspace handoff](../../docs/handoff.md).
