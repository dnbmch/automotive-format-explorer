# Signal Map View — DBC & LDF

Bit-level visualization of CAN/LIN message payloads. Signals are rendered at their exact bit positions with colored cells, byte boundaries, and endianness-correct layout (except ISO 17987 big-endian LIN signals, see [LDF simplification](#ldf-simplification)). Reuses the generic center panel slot.

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
| Byte order | Per-signal (LE or BE) | LE in LIN 1.3–2.2; ISO 17987 may declare BE (`LdfFile.big_endian_signals`), which the signal map ignores |
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

One row per byte, eight bit cells per row:
- **Color**: each cell takes the color of the signal that owns it; unoccupied bits are dark gray
- **Label**: the signal name, drawn across the signal's cells in its first byte row
- **Bit number**: column headers 7 to 0 above the grid; the byte index (`B0`, `B1`, …) in the left gutter
- **Byte boundary**: a thin line between byte rows
- **Endianness indicator**: a `←` after the name of a big-endian signal; little-endian signals carry no arrow

### LDF simplification

LIN 1.3–2.2 signals are little-endian, and LDF has no multiplexing. The signal map draws every LDF signal little-endian, ISO 17987 big-endian signals included ([BL-V3](../backlog.md#bl-v3-iso-17987-big-endian-lin-signals-are-drawn-little-endian)). The bit numbering is straightforward sequential. No endianness arrows appear.

## Rendering

### Grid layout

The grid is bit-oriented (not byte-oriented like A2L's memory view). Each cell represents one bit.

```
Standard CAN (8 bytes = 64 bits):
       7    6    5    4    3    2    1    0
    ┌────┬────┬────┬────┬────┬────┬────┬────┐
B0  │               EngineRPM               │
    ├────┼────┼────┼────┼────┼────┼────┼────┤
B1  │                                       │  (EngineRPM color, no label)
    ├────┼────┼────┼────┼────┼────┼────┼────┤
B2  │                Throttle               │
    ├────┼────┼────┼────┼────┼────┼────┼────┤
B3  │ F4 │ F3 │ F2 │ F1 │    Temperature    │
    ├────┼────┼────┼────┼────┼────┼────┼────┤
B4  │                                       │  (Temperature color, no label)
    ├────┼────┼────┼────┼────┼────┼────┼────┤
B5  │    ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  │  (unoccupied)
    ├────┼────┼────┼────┼────┼────┼────┼────┤
B6  │    ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  │
    ├────┼────┼────┼────┼────┼────┼────┼────┤
B7  │                Checksum               │
    └────┴────┴────┴────┴────┴────┴────┴────┘
```

- Row = one byte (8 bits across)
- Column headers: bit 7 (MSB) on left, bit 0 (LSB) on right (matches the standard CAN bit layout diagrams engineers expect)
- Byte index label on the left gutter
- 8 rows for CAN, up to 64 rows for CAN FD (scrollable)
- Each cell: 28x28px (`SignalGridItem::kCellSize`)

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
| 6 | Gold | Signal 6 |
| 7 | Indigo | Signal 7 |
| - | Dark gray | Unoccupied bits |

For messages with >8 signals, colors wrap with shade alternation (same as A2L). Multiplexed signals (DBC only) have no pattern of their own; under "All", bits that several mux groups claim carry the red overlap stripes.

### Signal labels

Each signal's name is drawn once, centered over its cells in the first byte row it occupies and elided at the right to that span's width. The tooltip on hover always shows full detail regardless of cell size.

### Hover tooltip

`SignalMapModel::signalTooltip()` builds it:

```
EngineRPM
Bits: [0..15] (16-bit, little-endian)
Physical: raw x 0.1  [0 .. 8000] rpm
Receivers: BCM, Dashboard
```

A multiplexed DBC signal adds `Multiplexor (M)`, `Multiplexed: m<N>` or `Mux: m<N> + multiplexor`. An LDF signal shows `Publisher: <node>` (the signal's own publisher) and no receivers line; its scaling comes from the first physical range of its encoding, and the bracketed range shows that range's raw bounds; logical encoding values are not shown.

### Multiplexing (DBC only)

Messages with multiplexed signals need special handling:

- **Multiplexor signal** (M): Always visible, rendered normally
- **Static signals**: Always visible, rendered normally
- **Multiplexed signals** (m0, m1, ...): shown in layers

UI approach: a mux-group ComboBox appears next to the message selector when the selected message has multiplexing. Options:
- "All" — shows every mux group; bits that several groups claim carry the overlap stripes
- "m0" — shows only signals for mux value 0
- "m1" — shows only signals for mux value 1
- etc. (one `m<N>` entry per mux value)

Default: "All" so the user sees the full picture, then can filter.

## Architecture

### `SignalMapModel` (`src/models/signalmapmodel.h`)

The data model, analogous to `MemoryMapModel` but for bit-level signal layout. It carries a list of `MessageEntry`, each holding a `std::vector<SignalEntry>`:

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
    quint64 nodeKey = 0;        // FrameSignal tree key
    quint64 signalNodeKey = 0;  // standalone Signal tree key
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

Query surface (`Q_INVOKABLE`): `signalAtBit(bit)` returns the signal index at a display bit position (-1 = unoccupied); `isSignalVisible(idx)` applies the current mux filter; `isOverlap(bit)` reports bits claimed by more than one signal; `signalTooltip(idx)` builds the rich tooltip with scaling, mux info, and sender/receivers. `bitPositions(sig)` is a public static converter from DBC bit numbering to physical display positions.

Internal data structure: a pre-computed **bit map** for the current message — `_bit_map`, one entry per `dlcBits` storing the signal index (-1 = unoccupied) — with a parallel `_overlap_map` flagging multiply-claimed bits. Both are rebuilt when `currentMessage` or `currentMuxGroup` changes.

For big-endian signals, `bitPositions()` resolves DBC Motorola bit numbering to physical display positions. Sessions reuse the same static method for DLC derivation, so a DLC=0 message and the grid agree on the effective length.

### `SignalGridItem` (`src/ui/signalgriditem.h`)

A `QQuickPaintedItem` that renders the bit-level grid. The data is small (max 512 bits for CAN FD, typically 64), so it fully repaints the visible region. Per byte row it draws the byte-index gutter, then each bit cell (7 down to 0): palette fill or unoccupied gray, red diagonal overlap stripes, the selection border, and the highlight-flash overlay; then the byte separator line and the signal-name labels spanning each signal's first byte row. DLC=0 renders a "no payload" message.

Keyboard: arrows/Tab cycle visible signals, PgUp/PgDn switch messages, Home/End jump to first/last, Enter/Space select in tree. CAN FD (DLC > 8) scrolls via Flickable + ScrollBar.

### `SignalMapView.qml` (`qml/components/SignalMapView.qml`)

The QML wrapper, analogous to `MemoryView.qml`: a toolbar (message selector ComboBox; mux-group ComboBox shown only when `muxGroupCount > 0`), the grid area (`SignalGridItem` + optional CAN FD scrollbar + tooltip overlay), a wrapping legend of color swatches, and a status bar (`"DLC: 8 bytes (64 bits) | 5 signals | 48/64 bits used (75%)"`).

It exposes a `mapModel` property (the `SignalMapModel`) and a `nodeKeyClicked(var)` signal, matching `MemoryView.qml` so the generic center-panel Loader binds it without special-casing. `scrollToNodeKey(nodeKey)` handles cross-message navigation: if the node belongs to a signal in a different message, it switches the message selector first, then highlights.

### Session wiring

`DbcDocumentSession` and `LdfDocumentSession` each own a `std::unique_ptr<SignalMapModel>`, override `centerPanelSource()` (returns `SignalMapView.qml`) and `centerPanelModel()` (returns the model), and populate it via a private `buildSignalMap()` that calls `finalize()` when done. DBC iterates `_document.messages()`; LDF iterates `_document.frames()`, mapping LDF fields onto the same `SignalEntry`/`MessageEntry` shapes with `bigEndian`/`multiplexType`/`isExtendedId` fixed to false/0/false, the message's `sender` from the frame publisher and each signal's from the signal's own publisher.

The generic center-panel Loader in Main.qml needs no format-specific wiring: it reads `centerPanelSource()`, passes `centerPanelModel()` as `mapModel`, and routes `nodeKeyClicked` the same way it does for the memory view.

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

Changing the message dropdown sets the current message and selects nothing in the tree.

The center-panel slot, `DocumentSession` interface, node-key bidirectional selection, and the Theme palette are shared with the A2L memory view. The model (`SignalMapModel`) and renderer (`SignalGridItem`) are signal-specific because the data is bit-level signal packing rather than a byte-level address space; the tooltip and legend reuse the memory view's patterns with signal-specific content (the legend is dynamic, showing the current message's signal names).

## Technical Notes

- Standard CAN (8 bytes) fits without scrolling. CAN FD (up to 64 bytes) uses Flickable + ScrollBar.
- The bit map is tiny (max 512 entries for CAN FD). Full rebuild on every message/mux change is fine.
- DBC Motorola (big-endian) bit numbering is implemented in `SignalMapModel::bitPositions()`. The same static method is reused for DLC derivation in both DBC and LDF sessions to guarantee consistency.
- `bitPositions()` caps at 512 bits and rejects negative startBit or zero bitLength to guard against malformed files.
- The `SignalMapModel` is format-agnostic. Both DBC and LDF sessions populate the same `MessageEntry`/`SignalEntry` structures. A future format (e.g., ARXML FIBEX) could reuse it.
- Color assignment: signals within a message get colors in declaration order (index mod 8). Shade alternation distinguishes adjacent same-color signals.
- The legend is dynamic (shows signal names for the current message) and wraps to multiple lines via Flow layout. Mux-filtered signals are hidden from the legend.
- DLC=0 messages derive their effective DLC from signal bit positions rather than being hidden.
