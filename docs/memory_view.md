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

### Generic center panel slot

The center panel is **not** A2L-specific infrastructure — it's a generic session-provided slot. Each `DocumentSession` subclass exposes a `centerPanelSource` property:

```cpp
Q_PROPERTY(QUrl centerPanelSource READ centerPanelSource CONSTANT)
```

- `A2lDocumentSession` returns `MemoryView.qml`
- `DbcDocumentSession` returns `SignalMapView.qml`
- `LdfDocumentSession` returns `SignalMapView.qml`

Main.qml uses a `Loader` in the center SplitView pane, bound to the active session's `centerPanelSource`. When empty, the center pane collapses and the layout falls back to the current two-column tree + detail.

The MemoryView component itself is fully A2L-specific. The generic part is just the slot plumbing (~20 lines in Main.qml + one property per session).

### Format-specific center content
- **A2L**: Memory view (hex grid) — see this document
- **DBC**: Signal map (bit-level CAN message layout) — see `signal_map.md`
- **LDF**: Signal map (bit-level LIN frame layout) — see `signal_map.md`

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

Click a colored cell in the memory view. The tree auto-scrolls to and selects the owning object. The detail panel updates to show its properties. If the byte is shared by multiple objects, show a disambiguation popup.

### Memory → memory

Click-drag to select a byte range. Status bar shows: `Selected: 0x80100 — 0x8010F (16 bytes) | 3 objects in range`. The detail panel lists all objects overlapping the selection.

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

Objects with Tier 1/2 sizes render with solid color blocks. Objects with Tier 3 layouts (where we can't compute exact size) render with a **dashed border** and use a best-guess size estimate (sum of component sizes without alignment). Tooltip shows `"Size: ~48 bytes (approximate — complex layout)"`.

This avoids the cascading visual error problem: an incorrect size for one object doesn't shift subsequent blocks because each object is placed at its own known address. Gaps and overlaps between objects are real (or indicate our size estimate is wrong, which the dashed border communicates).

### Measurement sub-byte precision

For Measurements with `bit_mask`, the footprint is the byte(s) containing the masked bits. Display as a partially-filled cell. Multiple measurements sharing the same byte(s) via different bit_masks are shown as subdivided cells.

## Implementation Status

### Implemented

- QQuickPaintedItem renderer (MemoryGridItem) with virtualized scrolling (only visible rows painted)
- Generic center panel slot: `centerPanelSource` / `centerPanelModel` on DocumentSession, Loader in Main.qml
- MemorySegment list with segment selector (dropdown when multiple segments), synthetic segment fallback
- Object filtering: Characteristics + AxisPts always included, Measurements only with `ecu_address`
- Sorted interval-based color/object maps for O(1) byte-level hit testing
- Tier 1 + Tier 2 size computation (VALUE, MEASUREMENT, CURVE, MAP, AXIS_PTS)
- Colored cells, address gutter, hover tooltips (name, type, address, size)
- Jump-to-address text field
- Click cell → select in tree + update detail
- Click tree node → scroll memory view + highlight flash
- Bytes-per-row toggle (8, 16, 32)
- Color legend with 8 object type categories
- Status bar: address range, object count, excluded measurement count
- Shade alternation for adjacent same-color objects

### Backlog

- Full RecordLayout interpretation (alignment, index modes, deposit modes) — Tier 3 sizes
- Gap detection (unassigned bytes between objects)
- Overlap detection (two objects claiming same bytes)
- Segment utilization percentage in header
- Export: segment map as CSV or HTML report
- Keyboard navigation within the grid

## Technical Notes

- The memory view is read-only visualization — no hex editing
- All data comes from the already-parsed protobuf, no file re-reading
- RecordLayout size computation is tiered — see Size Calculation section. Each object is placed at its own known address, so approximate sizes cause visual fuzziness at one object's boundary, not cascading errors
- **Performance**: typical CALIBRATION_VARIABLES segment is 64KB–256KB (4K–16K rows at 16 bytes/row) — well within QQuickPaintedItem's comfort zone. For outlier 2MB segments (128K rows), virtualized scrolling (only render visible rows + small buffer) is essential. The sorted interval vector for address lookup keeps hit-testing O(log n)
- **Renderer upgrade path**: if QQuickPaintedItem becomes a bottleneck (full repaint on scroll), swap to QQuickItem + QSGNode for incremental scene graph updates. The data model and QML interface stay the same — only the paint implementation changes
- Color scheme should respect the app's existing Theme.qml dark palette
- **AxisPts ownership**: when a Characteristic's AxisDescr has `AXIS_PTS_REF` pointing to a standalone AxisPts, the AxisPts is rendered as its own separate block (it occupies its own address range). This is not an overlap — do not show hatched pattern for this case
