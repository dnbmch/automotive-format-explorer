# Open verification — automotive-format-explorer

Checks no session can run here: they need the operator's eyes on a Windows desktop, the
16 GB reference laptop, or a licensed tool or standard. Each states what to check and
how pass differs from fail.

## One Windows launch of a fresh build

Operator-visual, with the four samples and a large recording (the generated 14-hour
files or a real one):

- Closing during a large MDF4 opening: open a large `.mf4` and close the window while
  the loading indicator shows. Pass: the window closes almost at once. Fail: it closes
  only after the opening finishes.
- File > Open lists "Automotive files (*.a2l *.dbc *.ldf *.mf4)", then A2L, DBC, LDF,
  MDF4 and All files, each narrowing the listing. The empty sidebar's four sample
  links open; long tab names elide with "…".
- In each sample, expand categories and entities, select a row and scroll; switch
  between the tabs, also in quick succession: each comes back as left.
- Type a filter, switch away and back: the filter text and filtered tree come back;
  clearing the filter restores the tree from before it, its selection included, also
  when nothing was selected or the selected row sat in a collapsed category.
- Close the current middle tab: its right neighbour is highlighted and shown. Close a
  tab left of the current one: the current plot keeps its zoom, a memory view its
  scroll and selection.
- Show the raw view on an A2L module row, then select other rows and an MDF4 channel:
  the JSON follows the selection. LDF's overview row offers no raw view.
- Closing the window during a large A2L load exits after the parse.
- An A2L whose objects span more than 16 MiB, for example RAM and flash without
  `MEMORY_SEGMENT`s: objects at both ends of the derived segment paint in their
  colors; scrolling or "Go to" reaches a far address and flash-highlights its object;
  clicking an object selects it in the tree; overlaps far into the segment are hatched.
- `demo_recording.mf4`: `speed` plots the sine with zoom, pan, hover and reset; `t`
  shows the master-channel notice. A foreign `.mf4`: numeric channels plot, exotic
  ones say "not plottable".
- Large recording: selecting a channel shows "Reading samples…" with a percentage,
  then the whole recording as vertical extrema columns with no line joining them; the
  header reads "Overview" and the sample count. A channel that fits one window turns
  to "Exact samples" shortly after.
- Hovering the overview highlights a band and reports a time range, an index range,
  the sample count and min/max; hovering exact samples shows a crosshair with the
  value and "index N".
- Zooming in shows a line through the exact samples, with gaps where values are
  missing; zooming out past about four million samples returns to the overview with
  "zoom in for exact samples". Reset view returns to the whole overview at once.
- Clicking several channels quickly, or zooming repeatedly, settles on the last one
  without showing an intermediate result; closing the tab during a long overview is
  prompt.
- A file whose source ends early shows "Incomplete: N of M samples", and still
  "Incomplete" with the center pane dragged to its narrowest; truncating or appending
  to an open file makes the next channel read "…the file changed since it was opened;
  reopen it".

Fail = a missing or unfiltered dialog entry, a dead sample link, clipped tabs; a tab
showing another tab's expansion or scroll, a lost filter or pre-filter tree, a
highlighted tab other than the one shown, a rebuilt plot, an empty or stale raw view;
an unoccupied cell where an object belongs, a scrollbar that cannot reach the far end,
a click that selects nothing; an empty `speed` plot or an empty plot without
explanation, a line joining overview columns, a hover naming a single sample over the
overview, a stale channel or view shown last, a short file presented as complete, a
stuck progress indicator, a close that hangs; a crash.

## The 16 GB reference laptop

Build `build-i2i3/b/measure/` (its `CMakeLists.txt`) against the landed reader and
Explorer and run its scenarios (`run_measurements.py`) there with the generated
14-hour recordings; the recorded figures come from a 64 GB host whose page cache held
the 10 GB file, and from builds before the reader's stream-trailer check. Pass = the
same order of overview and window times on a cold first read, cancellation well under
100 ms, peak commit about 75 MiB with a full window; fail = a peak that grows with the
recording, or a close or selection change that waits for a scan to finish.

## An ISO 17987 big-endian LDF beside a reference tool

The big-endian numbering the signal map draws, a scalar's offset as its MSB walked as a
DBC Motorola signal, rests on excerpts of ISO 17987-3:2025 §5.2.2.6.2 and its Figure 13
key ([LDF byte order](ref/signal_map.md#ldf-byte-order)). The workspace's own sample
contradicts it: in `ldf-parser/ldf_files/iso17987.ldf` (`LIN_sig_byte_order_big_endian`)
every signal sits on a byte edge, and under the drawn numbering `signal1` (16 bits at
offset 0 of the 2-byte `MotorControl`) starts at byte 0's bit 0 and runs past the frame's
end, while `MotorTemp` (8 bits at offset 8 of `MotorState_Cycl`) splits across bytes 1 and
2; a numbering from the signal's first byte puts them in bytes 0–1 and in byte 1. The
file's header (Vector DaVinci, created 2009, protocol ISO17987:2015) suggests a hand-edited
copy, so it settles nothing alone. Open it, and a copy holding a scalar of 9–16 bits at an
offset that is not a byte edge and a byte array, in the Explorer and in a tool that encodes
LIN frames from an LDF (Vector CANoe or LDF Explorer), or read them against ISO 17987-3:2025
§5.2.2.6.2 and Figure 13. Pass: for every signal the reference's bytes and bits equal the
Explorer's cells. Fail: any signal whose bits differ — the reference's numbering then
replaces the drawn one here (`LdfDocumentSession`) and in meas-signal's LDF adapter
(`to_descriptors`), with its source cited in § LDF byte order.
