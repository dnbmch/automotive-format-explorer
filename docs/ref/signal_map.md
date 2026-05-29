# Signal Map View — DBC & LDF

Bit-level visualization of CAN/LIN message payloads. Signals are rendered at their exact bit positions with colored cells, byte boundaries, and endianness-correct layout. Reuses the generic center panel slot.

## Why this matters

Signal bit-packing in DBC files is notoriously hard to get right — especially with mixed big/little endian signals in the same message. Existing tools charge real money for this visualization. LIN (LDF) frames have the same structure but are simpler (no multiplexing, no extended frames, always <= 8 bytes).

## Layout

Same three-column split as A2L. The signal map replaces the memory view in the center panel.

```
┌──────────────────────────────────────────────────────────────────┐
│ Tab Bar                                                          │
├──────────────┬─────────────────────────────┬─────────────────────┤
│              │                             │                     │
│  Tree        │  Signal Map View            │  Detail Panel       │
│  (nav)       │  (center content)           │  (right, always)    │
│              │                             │                     │
│  ▸ Messages  │  ┌─────────────────────┐   │  Signal: EngineRPM  │
│    ▸ 0x123   │  │ Message: 0x123 (▼)  │   │  ─────────────      │
│      ▸ RPM   │  ├─────────────────────┤   │  Start bit: 0       │
│      ▸ Temp  │  │ Byte 0    Byte 1    │   │  Length: 16         │
│    ▸ 0x456   │  │ ┌─┬─┬─┬─┬─┬─┬─┬─┐  │   │  Byte order: LE    │
│              │  │ │EngineRPM (blue) │  │   │  Factor: 0.1       │
│  ▸ Nodes     │  │ └─┴─┴─┴─┴─┴─┴─┴─┘  │   │  Offset: 0         │
│              │  │ Byte 2    Byte 3    │   │  Range: [0, 8000]   │
│              │  │ ┌─┬─┬─┬─┬─┬─┬─┬─┐  │   │  Unit: rpm          │
│              │  │ │Temp│   Flags   │  │   │                     │
│              │  │ └─┴─┴─┴─┴─┴─┴─┴─┘  │   │                     │
│              │  └─────────────────────┘   │                     │
├──────────────┴─────────────────────────────┴─────────────────────┤
│ DLC: 8 bytes (64 bits) | 5 signals | 48/64 bits used (75%)      │
└──────────────────────────────────────────────────────────────────┘
```

## Data Sources

### DBC (from `dbc.proto`)

| Proto field | What it provides |
|---|---|
| `Message.id` | CAN arbitration ID (11-bit or 29-bit extended) |
| `Message.name` | Human-readable message name |
| `Message.dlc` | Data length code (bytes: 0-8 CAN, 0-64 CAN FD) |
| `Message.signals[]` | List of signals packed into this message |
| `Signal.start_bit` | Start bit position (DBC bit numbering) |
| `Signal.bit_length` | Number of bits |
| `Signal.byte_order` | `LITTLE_ENDIAN` or `BIG_ENDIAN` (Motorola vs Intel) |
| `Signal.is_signed` | Signed/unsigned |
| `Signal.factor`, `offset` | Physical = raw * factor + offset |
| `Signal.min`, `max` | Physical value range |
| `Signal.unit` | Engineering unit string |
| `Signal.multiplex_type` | `NONE`, `MULTIPLEXOR`, `MULTIPLEXED` |
| `Signal.multiplex_value` | Mux switch value (when type == MULTIPLEXED) |

### LDF (from `ldf.proto`)

| Proto field | What it provides |
|---|---|
| `Frame.id` | LIN frame ID (0-63) |
| `Frame.name` | Frame name |
| `Frame.length` | Payload length in bytes (1-8) |
| `Frame.publisher` | Publishing node name |
| `Frame.signals[]` | Signals in this frame |
| `Signal.name` | Signal name |
| `Signal.start_bit` | Start bit offset in frame |
| `Signal.bit_length` | Number of bits |
| `Signal.init_value` | Initial/default value |
| `Signal.publisher` | Publishing node |
| `Signal.encoding` | Optional `SignalEncoding` (physical/logical mappings) |

### Key differences between DBC and LDF

| Aspect | DBC | LDF |
|---|---|---|
| Max payload | 8 bytes (CAN) / 64 bytes (CAN FD) | 8 bytes |
| Byte order | Per-signal (LE or BE) | Always LE (LIN spec) |
| Multiplexing | Yes (`M`, `m<N>`) | No |
| Extended ID | Yes (29-bit) | No (6-bit, 0-63) |
| Complexity | Higher | Lower |

Both map to the same visual model: a fixed-length byte payload with signals at bit offsets. The model can be unified.

## Bit Numbering & Endianness

This is the hardest part of the visualization and the primary source of user confusion in DBC files.

### DBC bit numbering convention

DBC files use a specific bit numbering scheme where bit 0 is the LSB of byte 0:

```
         Byte 0              Byte 1              Byte 2
Bit:  7  6  5  4  3  2  1  0 | 15 14 13 12 11 10  9  8 | 23 22 21 20 19 18 17 16
```

### Little-endian (Intel byte order)

Bits are numbered sequentially from `start_bit`. A 16-bit LE signal starting at bit 0 occupies bits 0-15 (byte 0 fully, byte 1 fully). Straightforward — bits flow left-to-right across bytes.

### Big-endian (Motorola byte order)

The `start_bit` in DBC is the MSB position in Motorola bit numbering. Bits flow in a zig-zag pattern across bytes. A 16-bit BE signal starting at bit 7 occupies bits 7,6,5,4,3,2,1,0 (byte 0) then 15,14,13,12,11,10,9,8 (byte 1).

But for signals that don't align to byte boundaries, the bit positions wrap in non-obvious ways. This is exactly what the visualization must clarify.

### Visual approach

Each bit cell shows:
- **Color**: which signal owns it
- **Label**: signal name (spanning the signal's bit range)
- **Bit number**: small superscript DBC bit number in each cell
- **Byte boundary**: thicker vertical line every 8 bits
- **Endianness indicator**: small arrow (→ for LE, ← for BE) in the signal label

For big-endian signals that wrap across bytes non-contiguously, the colored cells are still connected but with a visual "fold" indicator showing the byte crossing.

### LDF simplification

LDF is always little-endian, no multiplexing. The bit numbering is straightforward sequential. No endianness arrows needed.

## Rendering

### Grid layout

The grid is bit-oriented (not byte-oriented like A2L's memory view). Each cell represents one bit.

```
Standard CAN (8 bytes = 64 bits):
     Bit7 Bit6 Bit5 Bit4 Bit3 Bit2 Bit1 Bit0
    ┌────┬────┬────┬────┬────┬────┬────┬────┐
B0  │         EngineRPM (LE, 16-bit)        │
    ├────┼────┼────┼────┼────┼────┼────┼────┤
B1  │              EngineRPM                 │
    ├────┼────┼────┼────┼────┼────┼────┼────┤
B2  │    Throttle (LE, 8-bit)               │
    ├────┼────┼────┼────┼────┼────┼────┼────┤
B3  │ F4 │ F3 │ F2 │ F1 │    Temperature    │
    ├────┼────┼────┼────┼────┼────┼────┼────┤
B4  │              Temperature               │
    ├────┼────┼────┼────┼────┼────┼────┼────┤
B5  │    ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  │  (unoccupied)
    ├────┼────┼────┼────┼────┼────┼────┼────┤
B6  │    ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  │
    ├────┼────┼────┼────┼────┼────┼────┼────┤
B7  │ Checksum (LE, 8-bit)                  │
    └────┴────┴────┴────┴────┴────┴────┴────┘
```

- Row = one byte (8 bits across)
- Column headers: bit 7 (MSB) on left, bit 0 (LSB) on right (matches the standard CAN bit layout diagrams engineers expect)
- Byte index label on the left gutter
- 8 rows for CAN, up to 64 rows for CAN FD (scrollable)
- Each cell: ~24x24px for comfortable reading

### Coloring scheme

Signals get auto-assigned colors from a rotating palette. Same shade-alternation logic as A2L's MemoryGridItem (adjacent signals with the same color index get alternating light/dark shades).

Palette (8 colors, matching Theme):
| Index | Color | Usage |
|---|---|---|
| 0 | Blue | Signal 0 |
| 1 | Teal | Signal 1 |
| 2 | Purple | Signal 2 |
| 3 | Orange | Signal 3 |
| 4 | Green | Signal 4 |
| 5 | Cyan | Signal 5 |
| 6 | Yellow | Signal 6 |
| 7 | Indigo | Signal 7 |
| - | Dark gray | Unoccupied bits |

For messages with >8 signals, colors wrap with shade alternation (same as A2L). Multiplexed signals (DBC only) use a hatched/striped pattern over their base color.

### Signal labels

Signal names are rendered inside the colored region:
- If the signal spans >= 16 bits wide on screen: full name + bit info
- If 8-15 bits: abbreviated name
- If < 8 bits (single-bit flags): just the first letter or a symbol
- Tooltip on hover always shows full detail regardless of cell size

### Hover tooltip

```
Signal: EngineRPM
Bits: [0..15] (16 bits, little-endian)
Physical: raw × 0.1 + 0  →  [0.0 .. 8000.0] rpm
Sender: ECU1
Receivers: BCM, Dashboard
```

For LDF signals with encoding:
```
Signal: GearPosition
Bits: [0..2] (3 bits)
Init value: 0
Publisher: TCU
Encoding:
  0 = Park, 1 = Reverse, 2 = Neutral
  3 = Drive, 4 = Sport
```

### Multiplexing (DBC only)

Messages with multiplexed signals need special handling:

- **Multiplexor signal** (M): Always visible, rendered normally
- **Static signals**: Always visible, rendered normally
- **Multiplexed signals** (m0, m1, ...): shown in layers

UI approach: a secondary dropdown or toggle appears below the message selector when the selected message has multiplexing. Options:
- "All (overlay)" — shows all mux groups with hatched overlapping regions
- "m0: <mux_value_0>" — shows only signals for mux value 0
- "m1: <mux_value_1>" — shows only signals for mux value 1
- etc.

Default: "All (overlay)" so the user sees the full picture, then can filter.

## Architecture

### New C++ classes

#### `SignalMapModel` (`src/models/signalmapmodel.h`)

The data model, analogous to `MemoryMapModel` but for bit-level signal layout.

```cpp
struct SignalEntry {
    QString name;
    int startBit = 0;         // DBC bit number
    int bitLength = 0;
    bool bigEndian = false;    // true = Motorola byte order
    bool isSigned = false;
    double factor = 1.0;
    double offset = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    QString unit;
    int colorIndex = 0;
    quint64 nodeKey = 0;       // key into NodeRegistry
    int multiplexType = 0;     // 0=none, 1=multiplexor, 2=multiplexed, 3=both
    int multiplexValue = -1;   // mux switch value (-1 = N/A)
    QString sender;
    QStringList receivers;
};

struct MessageEntry {
    QString name;
    uint32_t id = 0;
    int dlc = 0;               // bytes (0 = derived from signals)
    bool isExtendedId = false;  // DBC 29-bit
    QString sender;
    std::vector<SignalEntry> signalEntries;
    quint64 nodeKey = 0;
};
```

Key methods on `SignalMapModel`:

```cpp
// Bit query: returns signal index at a display bit position, or -1.
Q_INVOKABLE int signalAtBit(int bitPosition) const;

// Mux filtering: -1 = all, 0..N-1 = index into sorted mux values.
Q_INVOKABLE bool isSignalVisible(int signalIndex) const;
Q_INVOKABLE bool isOverlap(int bitPosition) const;

// Rich tooltip with scaling, mux info, sender/receivers.
Q_INVOKABLE QString signalTooltip(int signalIndex) const;

// DBC bit numbering resolution (public static for DLC derivation reuse).
static std::vector<int> bitPositions(const SignalEntry& sig);
```

Key internal data structure: a pre-computed **bit map** for the current message. Array of `dlcBits` entries, each storing the signal index (-1 = unoccupied). A parallel `_overlap_map` tracks bits claimed by multiple signals. Both are rebuilt when `currentMessage` or `currentMuxGroup` changes.

For big-endian signals, `bitPositions()` resolves DBC Motorola bit numbering to physical display positions. The same method is reused by session `buildSignalMap()` for DLC derivation, guaranteeing consistency.

#### `SignalGridItem` (`src/ui/signalgriditem.h`)

QQuickPaintedItem that renders the bit-level grid. Simpler than MemoryGridItem because the data is small (max 512 bits for CAN FD, typically 64).

Paint logic:
1. Draw column headers (bit 7..0)
2. For each byte row (0..dlcBytes-1):
   a. Draw byte index in left gutter
   b. For each bit (7 down to 0):
      - Fill cell with palette color (or unoccupied gray)
      - Draw red diagonal stripes for overlapping bits
      - Draw selection border for selected signal
      - Draw highlight flash overlay
   c. Draw byte separator line
3. Draw signal name labels in the first byte row of each signal's span
4. Handle DLC=0 with "no payload" text

Keyboard: arrows/Tab cycle visible signals, PgUp/PgDn switch messages, Home/End jump to first/last, Enter/Space select in tree. Scrolling via Flickable + ScrollBar for CAN FD (DLC > 8).

### New QML component

#### `SignalMapView.qml` (`qml/components/SignalMapView.qml`)

Analogous to `MemoryView.qml`. Structure:

```
ColumnLayout:
  ┌─ Toolbar (32px)
  │   Message selector ComboBox
  │   [Mux group selector ComboBox — visible only when muxGroupCount > 0]
  │
  ├─ Grid area (fill)
  │   SignalGridItem + optional scrollbar (CAN FD only)
  │   Tooltip overlay
  │
  ├─ Legend (24px)
  │   Color swatches with signal names for current message
  │
  └─ Status bar (22px)
      "DLC: 8 bytes (64 bits) | 5 signals | 48/64 bits used (75%)"
```

Properties:
```qml
required property var mapModel  // SignalMapModel instance
signal signalClicked(int signalIndex)
signal nodeKeyClicked(var nodeKey)

function scrollToNodeKey(nodeKey) {
    // Find signal, select message if needed, highlight signal
}
```

The `scrollToNodeKey` function must handle cross-message navigation: if the nodeKey belongs to a signal in a different message than currently displayed, switch the message selector first, then highlight.

### Session wiring

#### `DbcDocumentSession` changes

```cpp
class DbcDocumentSession final : public AdapterSessionBase {
    // ...existing...

    QUrl centerPanelSource() const override;    // returns SignalMapView.qml
    QAbstractListModel* centerPanelModel() override;  // returns SignalMapModel*

private:
    void buildSignalMap();  // NEW: populate SignalMapModel from dbc::DbcFile
    std::unique_ptr<SignalMapModel> _signal_map_model;
};
```

`buildSignalMap()` iterates `_document.messages()`, creates `MessageEntry` for each, populates signals with color assignment, and calls `finalize()`.

#### `LdfDocumentSession` changes

```cpp
class LdfDocumentSession final : public AdapterSessionBase {
    // ...existing...

    QUrl centerPanelSource() const override;    // returns SignalMapView.qml
    QAbstractListModel* centerPanelModel() override;  // returns SignalMapModel*

private:
    void buildSignalMap();
    std::unique_ptr<SignalMapModel> _signal_map_model;
};
```

Same model, same QML, same grid item. `buildSignalMap()` iterates `_document.frames()`, maps LDF fields to the same `SignalEntry`/`MessageEntry` structures. LDF-specific differences:
- `bigEndian` is always `false`
- `multiplexType` is always `0`
- `isExtendedId` is always `false`
- `publisher` is populated from frame's publisher field
- `initValue` is populated from signal's init_value

### Loader integration (Main.qml)

No changes needed to Main.qml. The existing `centerPanelLoader` pattern works as-is:
- Session returns `centerPanelSource()` → Loader loads the QML
- Session returns `centerPanelModel()` → Loader passes it as `mapModel` property
- `nodeKeyClicked` signal connection already wired generically

The only requirement: `SignalMapView.qml` must expose the same interface as `MemoryView.qml` — a `mapModel` property and `nodeKeyClicked(var)` signal. (It can also expose `signalClicked` for format-specific use.)

## Bidirectional Selection

### Tree → Signal Map

Click a signal in the tree:
1. `AppController` calls `selectCurrentNode(nodeKey)`
2. `SignalMapView.scrollToNodeKey(nodeKey)` is invoked via the existing `navPanel` connection
3. If the signal's message isn't currently displayed, switch `currentMessage`
4. Highlight the signal's bits with a pulse animation

Click a message in the tree:
1. Same flow, but `messageIndexForNodeKey` switches to that message
2. No individual signal highlight

### Signal Map → Tree

Click a colored bit cell:
1. `SignalGridItem` emits `nodeKeyClicked(nodeKey)` for the owning signal
2. Main.qml's existing connection calls `AppController.selectCurrentNode(nodeKey)` + `navPanel.selectAndScrollTo(nodeKey)`
3. Tree scrolls to signal, detail panel updates

### Message selector → Tree

When the user changes the message dropdown, optionally select the message node in the tree (debatable — may be annoying). Probably don't auto-select on dropdown change, only on explicit grid click.

## What's shared vs. new

| Component | Reuse from A2L | New for DBC/LDF |
|---|---|---|
| Center panel slot (Main.qml) | 100% reused | Nothing |
| `DocumentSession` interface | 100% reused | Nothing |
| `NodeRegistry` / bidirectional selection | 100% reused | Nothing |
| Theme colors / palette | Reused (same 8-color palette) | Nothing |
| `MemoryMapModel` | Not reused | `SignalMapModel` (different data model) |
| `MemoryGridItem` | Not reused (byte-oriented) | `SignalGridItem` (bit-oriented) |
| `MemoryView.qml` | Not reused | `SignalMapView.qml` (similar structure) |
| Tooltip pattern | Pattern reused | New content |
| Legend pattern | Pattern reused | Dynamic (signal names, not fixed types) |

The infrastructure (center panel slot, NodeRegistry, bidirectional selection, Theme) is fully reusable. The model and renderer are new because the data structure is fundamentally different (bit-level signal packing vs. byte-level address space).

## Implementation Status

Fully implemented. Export/print is backlog.

- SignalMapModel with message list, signal entries, bit map, mux filtering
- LE + BE (Motorola) bit position resolution with visual endianness arrows
- SignalGridItem renderer: bit cells, byte rows, color fills, signal labels, overlap stripes
- SignalMapView.qml: message selector, mux group dropdown, grid with scrollbar, wrapping legend, status bar
- DBC + LDF session wiring with DLC derivation from signals when DLC=0
- Rich tooltips: bit range, byte order, scaling, mux info, sender/receivers
- Bidirectional tree/grid navigation
- Mux group selector: "All" + per-mux-value filter, legend/grid update on filter change
- CAN FD scrolling via Flickable + ScrollBar for messages > 8 bytes
- Overlap detection with red diagonal stripe pattern
- Keyboard navigation: arrows/Tab cycle signals, PgUp/PgDn switch messages, Home/End, Enter/Space select

### Backlog

- Export: message layout as PNG or SVG (for documentation)
- Print-friendly view

## Technical Notes

- Standard CAN (8 bytes) fits without scrolling. CAN FD (up to 64 bytes) uses Flickable + ScrollBar.
- The bit map is tiny (max 512 entries for CAN FD). Full rebuild on every message/mux change is fine.
- DBC Motorola (big-endian) bit numbering is implemented in `SignalMapModel::bitPositions()`. The same static method is reused for DLC derivation in both DBC and LDF sessions to guarantee consistency.
- `bitPositions()` caps at 512 bits and rejects negative startBit or zero bitLength to guard against malformed files.
- The `SignalMapModel` is format-agnostic. Both DBC and LDF sessions populate the same `MessageEntry`/`SignalEntry` structures. A future format (e.g., ARXML FIBEX) could reuse it.
- Color assignment: signals within a message get colors in declaration order (index mod 8). Shade alternation distinguishes adjacent same-color signals.
- The legend is dynamic (shows signal names for the current message) and wraps to multiple lines via Flow layout. Mux-filtered signals are hidden from the legend.
- DLC=0 messages derive their effective DLC from signal bit positions rather than being hidden.

## File inventory

New files:
```
src/models/signalmapmodel.h
src/models/signalmapmodel.cpp
src/ui/signalgriditem.h
src/ui/signalgriditem.cpp
qml/components/SignalMapView.qml
```

Modified files:
```
src/sessions/dbcdocumentsession.h    (_signal_map_model, centerPanel overrides, _tree_node_keys)
src/sessions/dbcdocumentsession.cpp  (buildSignalMap(), nodeKey capture in buildTree())
src/sessions/ldfdocumentsession.h    (_signal_map_model, centerPanel overrides, _tree_node_keys)
src/sessions/ldfdocumentsession.cpp  (buildSignalMap() with signal lookup enrichment)
src/main.cpp                         (SignalGridItem QML type registration)
CMakeLists.txt                       (new source files + QML)
qml/components/Theme.qml            (signalColors palette)
qml/components/NavPanel.qml         (signal map keyboard shortcuts in help popup)
```
