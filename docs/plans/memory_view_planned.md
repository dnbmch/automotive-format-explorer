# Memory view — planned enhancements

Unbuilt behaviour for the A2L memory grid (`MemoryGridItem`). Current behaviour
is in [../ref/memory_view.md](../ref/memory_view.md); this file holds the design
for features not yet implemented so the reference doc stays factual.

## Range-selection detail listing

Click-drag byte-range selection with the status-bar readout is built. Still
planned: the detail panel listing every object overlapping the selection
(currently the detail panel follows only single-object selection).

## Disambiguation popup

When a clicked byte is shared by multiple objects, present a small popup to pick
which object to select, instead of selecting a single owner.

## Sub-byte subdivided cells

For measurements with a `bit_mask`, render the footprint byte as a subdivided
cell (or half-filled cell), so multiple measurements sharing a byte through
different masks are distinguishable. The whole-byte overlap hatching treats
mask-sharing measurements as overlapping; sub-byte rendering would resolve
that visually.

## Dashed border for approximate-size blocks

Objects whose size is only approximate (Tier 3 layouts, where exact size needs
full `RecordLayout` interpretation) currently render as solid blocks like every
other object; the tooltip is the only signal that the size is a best guess.
Planned: draw approximate-size blocks with a **dashed border** so the
approximation reads visually, not just in the tooltip.

## Other backlog items

- Full `RecordLayout` interpretation for exact sizes (Tier 3): alignment padding
  between components, `ALTERNATE_*` index modes, `DEPOSIT_MODE`, fixed-vs-dynamic
  axis-point counts, CUBOID/CUBOID4/CUBOID5, `VAL_BLK` with `matrix_dim`.
- Gap detection (unassigned bytes between objects).
- Segment utilization percentage in the header.
- Export: segment map as CSV or HTML report.
- Keyboard navigation within the grid (matching the signal grid).
