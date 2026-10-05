# Memory View

Visual ECU memory map for A2L files. Calibration parameters and measurements are laid out at their ECU addresses as colored blocks, alongside the tree and detail panel.

## Layout

Three-column split. Tree and detail panel are always visible. The memory view occupies the center content area.

```
┌─────────────────────────────────────────────────────────┐
│ Tab Bar                                                 │
├────────────┬────────────────────────┬───────────────────┤
│            │                        │                   │
│  Tree      │   Memory View          │  Detail Panel     │
│  (nav)     │   (center content)     │  (right, always)  │
│  (always)  │                        │                   │
│            │  ┌──────────────────┐  │  Identity         │
│  ▸ Module  │  │ Segment selector │  │  ───────────      │
│    ▸ Meas  │  ├──────────────────┤  │  Name: foo        │
│    ▸ Chars │  │ 0x80000 ████████ │  │  Type: VALUE      │
│    ▸ Axis  │  │ 0x80004 ████░░░░ │  │  Address: 0x80004 │
│    ▸ Compu │  │ 0x80008 ████████ │  │  ...              │
│    ▸ Units │  │ 0x8000C ░░░░░░░░ │  │                   │
│    ▸ Proto │  │ 0x80010 ████████ │  │  Axis Description  │
│            │  │ ...              │  │  ───────────      │
│            │  └──────────────────┘  │  ...              │
│            │                        │                   │
├────────────┴────────────────────────┴───────────────────┤
│ Status: 0x80100 — 0x8010F (16 bytes) | 3 objects       │
└─────────────────────────────────────────────────────────┘
```

All three panels coexist. The vertical split handles between tree|memory and memory|detail are draggable, so the user can allocate space to taste.

The memory view fills the generic, session-provided center-panel slot; which view each format puts there is in the [adapter contract](../arch/adapter_contract.md#center-panel).

## Data Sources (from A2L protobuf)

| Proto message | What it provides |
|---|---|
| `MemorySegment` | Named memory regions with base address, size, type (FLASH/RAM/ROM), prg_type (CAL/CODE/DATA) |
| `Characteristic` | Calibration parameter: address, record_layout_ref, type (VALUE/CURVE/MAP/...), byte_order |
| `Measurement` | Runtime signal: ecu_address (optional!), datatype, bit_mask |
| `AxisPts` | Standalone axis: address, record_layout_ref |
| `RecordLayout` | Byte-level structure: field positions, data types, axis layout, alignment |
| `ModCommon` | Default byte_order, alignment, deposit_mode |

### Filtering: objects without addresses

Not all objects can be placed on the memory grid:
- **Measurements** often lack `ecu_address` — their address lives only in XCP/CCP `IF_DATA` blocks, which we don't parse into a usable address. These measurements are **excluded** from the grid.
- **Characteristics and AxisPts** always have an `address` field (required in the spec), so they are always included.

The status bar shows a count of excluded objects: `"12 measurements without ECU address (not shown)"`.

## Rendering

### Hex grid

Each row = 16 bytes (configurable: 8, 16, 32). Left gutter shows absolute address in hex. Each byte cell is a small rectangle. The status bar shows the segment's address range and its object count.

### Coloring scheme

| Object type | Color | Notes |
|---|---|---|
| Characteristic (VALUE) | Blue | Single scalar calibration value |
| Characteristic (CURVE) | Teal | 1D lookup table |
| Characteristic (MAP) | Purple | 2D lookup table |
| Characteristic (CUBOID+) | Indigo | Multi-dimensional |
| Characteristic (ASCII) | Orange | String parameter |
| Characteristic (VAL_BLK) | Cyan | Array of values |
| Measurement | Green | Runtime signal overlay |
| AxisPts | Gold | Standalone axis distribution |
| Unoccupied (in segment) | Dark gray | Allocated but not assigned |
| Outside segment | Background | No data |

A legend below the grid names the eight object-type colors. Adjacent same-color objects alternate between the base color and a darker shade, assigned once per segment in address order, so scrolling never changes a color.

### Overlap hatching

Bytes claimed by more than one object (e.g. a measurement aliasing a characteristic address) draw diagonal red hatching over the cell fill, matching the signal grid's overlap marker. Detection lives in the model's byte query, `MemoryMapModel::queryBytes`, which decides ownership and overlap together from the segment's object intervals; `isOverlap(address)` asks it about one byte. Sub-byte `bit_mask` footprints still color (and hatch) whole bytes — subdivided cells are a [planned enhancement](../plans/memory_view_planned.md).

### Hover tooltip

On hover over a colored cell, a floating tooltip shows name, type, address, size, and — when the object carries them — record layout and conversion method:
```
KfAIRCTL_tTransDly
Type: CURVE  |  Address: 0x80100  |  Size: 24 bytes
Record Layout: RL_CURVE_FLOAT32
Conversion: CM_KfAIRCTL_tTransDly
```

### Segment selector

Dropdown at the top of the memory view listing all `MemorySegment` entries. Each option shows:
```
CAL_FLASH  [0x80000 .. 0x8FFFF]  FLASH / CALIBRATION_VARIABLES
```

Selecting a segment scrolls the grid to that region and only colors objects within it.

#### Fallback when no segments are defined

Many production A2L files have no `MemorySegment` entries at all — segments are optional in the spec. When no segments exist, the memory view derives a **synthetic range** from the objects themselves:

1. Collect all addresses from Characteristics, AxisPts, and address-bearing Measurements
2. Compute `min(all addresses)` and `max(all addresses + object size)`
3. Align start down and end up to the nearest 256-byte boundary
4. Present this as a single synthetic segment: `"[derived]  [0x80000 .. 0x8FFFF]"`

The segment selector dropdown is hidden when there is only one segment (real or synthetic). The grid is always available if there is at least one addressable object.

## Bidirectional Selection

### Tree/detail → memory

Click a Characteristic, Measurement, or AxisPts in the tree. The memory view scrolls to that object's address and highlights its byte range with a bright selection border (pulsing or thicker outline). The detail panel shows the text properties.

### Memory → tree/detail

Click a colored cell in the memory view. The tree auto-scrolls to and selects the owning object. The detail panel updates to show its properties. A byte shared by several objects belongs to the one drawn last: the highest start address, and among objects starting at one address the later in document order. That owner is painted, hit and selected; a disambiguation popup is a [planned enhancement](../plans/memory_view_planned.md).

### Memory → memory

Click-drag selects a byte range: press anchors the selection, dragging extends it (clamped to the grid), and the selected bytes render with a translucent white overlay. The status bar switches to a range readout — `Selected: 0x80100 — 0x8010F (16 bytes) | 3 objects in range` — with the object count from `MemoryMapModel::objectsInRange`. A plain click clears the range and selects the byte's owning object as before; segment or model changes also clear it. The detail panel listing every object in the range is a [planned enhancement](../plans/memory_view_planned.md).

The "Go to" address field scrolls to the entered address and flash-highlights the object at it (`MemoryMapModel::objectAtAddress`), if any.

## Size Calculation

Size calculation is the hardest part of this feature. The ASAP2 spec has complex layout rules with alignment padding, index modes, deposit modes, and axis interleaving. We use a **tiered approach**: handle common cases correctly, mark complex cases as approximate.

### Tier 1 — Trivially correct

These cases have straightforward size computation:

- **VALUE**: Look up RecordLayout, find `function_values` component → `sizeof(datatype)`. If no RecordLayout match, fall back to the Characteristic's conversion method to infer type.
- **Measurement**: `sizeof(datatype)` from the measurement's `datatype` field. For array measurements, multiply by `array_size` or product of `matrix_dim`.
- **AxisPts (simple)**: `max_axis_points * sizeof(axis datatype)` + header fields from RecordLayout.

### Tier 2 — Common but non-trivial

- **CURVE** with `ROW_DIR` index mode: axis values block + function values block, laid out sequentially. Size = `(max_axis_points * sizeof(axis_type)) + (max_axis_points * sizeof(value_type))` + any `NO_AXIS_PTS_X` header field.
- **MAP** with `ROW_DIR`: similar, but two axis dimensions. Size = axis_x block + axis_y block + (x_count * y_count * sizeof(value_type)).

### Tier 3 — Complex (backlog)

These require full RecordLayout interpretation:

- **`ALTERNATE_CURVES` / `ALTERNATE_WITH_X`** index modes — axis and value bytes are interleaved, not blocked
- **Alignment padding** between RecordLayout components (`ALIGNMENT_BYTE`, `ALIGNMENT_WORD`, `ALIGNMENT_LONG`, `ALIGNMENT_FLOAT32_IEEE`, `ALIGNMENT_FLOAT64_IEEE`) — must be applied between fields, not just at the end
- **`DEPOSIT_MODE`** (ABSOLUTE vs DIFFERENCE) — affects axis memory interpretation
- **`FIX_NO_AXIS_PTS_X/Y`** vs dynamic `NO_AXIS_PTS_X/Y`** — static vs dynamic axis count stored in memory
- **CUBOID, CUBOID4, CUBOID5** — multi-dimensional with 3-5 axes
- **VAL_BLK with matrix_dim** — multi-dimensional value arrays

### Visual treatment of approximate sizes

All objects render with solid color blocks. Objects with Tier 3 layouts (where we can't compute exact size) use a best-guess size estimate (sum of component sizes without alignment) and the tooltip marks the size approximate, e.g. `"Size: ~48 bytes (approx)"`. A distinct **dashed border** for approximate-size blocks is a [planned enhancement](../plans/memory_view_planned.md).

This avoids the cascading visual error problem: an incorrect size for one object doesn't shift subsequent blocks because each object is placed at its own known address. Gaps and overlaps between objects are real (or indicate our size estimate is wrong).

### Measurement sub-byte precision

For Measurements with `bit_mask`, the footprint is the byte(s) containing the masked bits. The current grid colors the whole containing byte(s); subdivided/partially-filled cells for sub-byte footprints are a [planned enhancement](../plans/memory_view_planned.md).

## Planned

Tier 3 sizing, gap detection, sub-byte subdivided cells, dashed
approximate-size borders, the range-selection detail listing, the
disambiguation popup, segment utilization, export, and grid keyboard
navigation are designed in
[docs/plans/memory_view_planned.md](../plans/memory_view_planned.md).

## Technical Notes

- The memory view is read-only visualization — no hex editing
- All data comes from the already-parsed protobuf, no file re-reading
- RecordLayout size computation is tiered — see Size Calculation section. Each object is placed at its own known address, so approximate sizes cause visual fuzziness at one object's boundary, not cascading errors
- **Performance**: each paint allocates a tile for the visible rows. Query work follows that tile and the candidate interval range selected by the prefix maximum ends; a long early interval can keep already-ended objects in that scan. There is no allocation proportional to the segment span. Hit-testing uses the same query for one byte
- **Coordinate limits**: row counts and interval arithmetic support multi-GiB spans, and ends saturate at the top of the address space. UI scrolling uses floating coordinates and selection uses signed byte offsets, so this is not a guarantee of precise interaction over every possible 64-bit span. A high base address alone does not imply a large span
- **Renderer upgrade path**: if QQuickPaintedItem becomes a bottleneck (full repaint on scroll), swap to QQuickItem + QSGNode for incremental scene graph updates. The data model and QML interface stay the same — only the paint implementation changes
- Color scheme should respect the app's existing Theme.qml dark palette
- **AxisPts ownership**: when a Characteristic's AxisDescr has `AXIS_PTS_REF` pointing to a standalone AxisPts, the AxisPts is rendered as its own separate block (it occupies its own address range). This is not an overlap — do not show hatched pattern for this case
