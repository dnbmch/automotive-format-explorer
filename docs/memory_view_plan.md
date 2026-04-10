# Memory View — Design Plan

A visual ECU memory map for A2L files. Shows calibration parameters and measurements laid out at their ECU addresses with colored blocks, alongside the existing text detail panel.

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

The center panel is format-specific visualization:
- **A2L with memory segments**: Memory view (hex grid)
- **A2L without memory segments / DBC / LDF**: Empty or future format-specific visualization
- The center panel concept is extensible — DBC could show a CAN matrix view, LDF could show a schedule timeline

## Data Sources (from A2L protobuf)

| Proto message | What it provides |
|---|---|
| `MemorySegment` | Named memory regions with base address, size, type (FLASH/RAM/ROM), prg_type (CAL/CODE/DATA) |
| `Characteristic` | Calibration parameter: address, record_layout_ref, type (VALUE/CURVE/MAP/...), byte_order |
| `Measurement` | Runtime signal: ecu_address, datatype, bit_mask |
| `AxisPts` | Standalone axis: address, record_layout_ref |
| `RecordLayout` | Byte-level structure: field positions, data types, axis layout, alignment |
| `ModCommon` | Default byte_order, alignment, deposit_mode |

## Rendering

### Hex grid

Each row = 16 bytes (configurable: 8, 16, 32). Left gutter shows absolute address in hex. Each byte cell is a small rectangle.

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
| AxisPts | Yellow | Standalone axis distribution |
| Unoccupied (in segment) | Dark gray | Allocated but not assigned |
| Outside segment | Background | No data |

Overlapping objects (e.g. measurement aliasing a characteristic address) get a hatched/striped pattern. Byte-level precision — partial bytes from bit_mask show as half-filled.

### Hover tooltip

On hover over a colored cell, show a floating tooltip:
```
CHARACTERISTIC "KfAIRCTL_tTransDly"
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

## Bidirectional Selection

### Tree/detail → memory

Click a Characteristic, Measurement, or AxisPts in the tree. The memory view scrolls to that object's address and highlights its byte range with a bright selection border (pulsing or thicker outline). The detail panel shows the text properties.

### Memory → tree/detail

Click a colored cell in the memory view. The tree auto-scrolls to and selects the owning object. The detail panel updates to show its properties. If the byte is shared by multiple objects, show a disambiguation popup.

### Memory → memory

Click-drag to select a byte range. Status bar shows: `Selected: 0x80100 — 0x8010F (16 bytes) | 3 objects in range`. The detail panel lists all objects overlapping the selection.

## Size Calculation

For each Characteristic/AxisPts, compute byte footprint from its RecordLayout:

1. Look up the RecordLayout by `record_layout_ref`
2. Walk the ordered `components` list
3. For each component, compute byte size from its `DataType` and position
4. The last component's `position + sizeof(datatype)` gives the total
5. For CURVE/MAP types, multiply by axis dimensions from AxisDescr `max_axis_points`
6. Apply alignment from RecordLayout alignment components and ModCommon defaults

For Measurements, size = `sizeof(DataType)` from the measurement's `datatype` field. Apply `bit_mask` for sub-byte precision.

## Implementation Phases

### Phase 1 — Static grid with colored blocks

- Custom QQuickPaintedItem or QQuickItem with Canvas for the hex grid
- Parse MemorySegment list, build segment selector
- For selected segment, collect all Characteristic/Measurement/AxisPts whose address falls within range
- Compute byte footprint per object using RecordLayout
- Render colored cells, address gutter, hover tooltips
- Wire tree selection → memory scroll

### Phase 2 — Bidirectional selection

- Click cell → select in tree + update detail
- Click-drag range selection
- Disambiguation popup for overlapping objects
- Status bar with selection info

### Phase 3 — Layout analysis

- Gap detection (unassigned bytes between objects)
- Overlap detection (two objects claiming same bytes)
- Segment utilization percentage in header
- Export: segment map as CSV or HTML report

## Technical Notes

- The memory view is read-only visualization — no hex editing
- All data comes from the already-parsed protobuf, no file re-reading
- RecordLayout size computation is the hard part — the ASAP2 spec has complex layout rules with alignment padding, index modes, and axis interleaving. Phase 1 can use a simplified calculation (sum of component sizes + padding estimate), refined in Phase 3
- Performance: a typical CALIBRATION_VARIABLES segment is 64KB–2MB. At 16 bytes/row, that's 4K–128K rows. Use virtualized scrolling (only render visible rows)
- Color scheme should respect the app's existing Theme.qml dark palette
