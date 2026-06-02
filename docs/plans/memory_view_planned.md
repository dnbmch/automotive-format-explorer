# Memory view — planned enhancements

Unbuilt behaviour for the A2L memory grid (`MemoryGridItem`). Current behaviour
is in [../ref/memory_view.md](../ref/memory_view.md); this file holds the design
for features not yet implemented so the reference doc stays factual.

## Overlap visualization

The signal grid (`SignalGridItem`) already draws diagonal red hatching on bits
claimed by more than one signal. The memory grid has no equivalent yet. Planned:
hatched/striped fill on bytes claimed by more than one object (e.g. a measurement
aliasing a characteristic address), and half-filled cells for partial bytes
covered by a `bit_mask`.

Note the non-overlap case: when a Characteristic's `AxisDescr` has `AXIS_PTS_REF`
to a standalone `AxisPts`, the AxisPts occupies its own address range and is
rendered as a separate block — that is not an overlap and must not be hatched.

## Rich hover tooltip

The current tooltip emits name / type / address / size. Planned: add the record
layout and conversion method, e.g.

```
CHARACTERISTIC "KfAIRCTL_tTransDly"
Type: CURVE  |  Address: 0x80100  |  Size: 24 bytes
Record Layout: RL_CURVE_FLOAT32
Conversion: CM_KfAIRCTL_tTransDly
```

## Byte-range selection (Memory → memory)

Current: single click selects the byte's owning object. Planned: click-drag to
select a byte range, with a status-bar readout
`Selected: 0x80100 — 0x8010F (16 bytes) | 3 objects in range`, and the detail
panel listing every object overlapping the selection.

## Disambiguation popup

When a clicked byte is shared by multiple objects, present a small popup to pick
which object to select, instead of selecting a single owner.

## Sub-byte subdivided cells

For measurements with a `bit_mask`, render the footprint byte as a subdivided
cell, so multiple measurements sharing a byte through different masks are
distinguishable.

## Other backlog items

- Full `RecordLayout` interpretation for exact sizes (Tier 3): alignment padding
  between components, `ALTERNATE_*` index modes, `DEPOSIT_MODE`, fixed-vs-dynamic
  axis-point counts, CUBOID/CUBOID4/CUBOID5, `VAL_BLK` with `matrix_dim`.
- Gap detection (unassigned bytes between objects).
- Overlap detection (two objects claiming the same bytes) feeding the
  visualization above.
- Segment utilization percentage in the header.
- Export: segment map as CSV or HTML report.
- Keyboard navigation within the grid (matching the signal grid).
