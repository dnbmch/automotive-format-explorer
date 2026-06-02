# Keyboard reference

All keyboard input handled by the explorer.

## Application shortcuts

Defined in [qml/Main.qml](../../qml/Main.qml).

| Shortcut | Action |
|---|---|
| `Ctrl+O` | Open file dialog |
| `Ctrl+W` | Close the current tab (no-op if no tab is open) |
| `Ctrl+Tab` | Next tab (wraps) |
| `Ctrl+Shift+Tab` | Previous tab (wraps) |
| `Ctrl+Alt+B` | Toggle the left navigation sidebar |

## Signal grid (DBC / LDF)

Defined in [src/ui/signalgriditem.cpp](../../src/ui/signalgriditem.cpp). The grid must have focus — click on it once or Tab to it from the tree.

| Key | Action |
|---|---|
| `Tab`, `→`, `↓` | Select next signal in the current message |
| `Shift+Tab`, `←`, `↑` | Select previous signal |
| `Home` | Jump to the first message |
| `End` | Jump to the last message |
| `PageDown` | Next message |
| `PageUp` | Previous message |
| `Return`, `Enter`, `Space` | Emit click on the currently selected signal — updates the detail panel and the tree highlight |

## Memory grid (A2L)

Defined in [src/ui/memorygriditem.cpp](../../src/ui/memorygriditem.cpp). The grid currently responds to mouse input only (single click to select a byte); no keyboard handler is wired up. A keyboard navigation block matching the signal grid is a planned enhancement — see [docs/plans/memory_view_planned.md](../plans/memory_view_planned.md).

## Tree (NavPanel)

The Qt `QQuickTreeView` ships with built-in keyboard navigation: arrow keys move between visible items, `+`/`-` expand/collapse, `Return` activates the current item. Selection updates flow through `AppController::selectCurrentNode()` and refresh the detail panel and any open center-panel view.
