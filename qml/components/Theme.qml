pragma Singleton
import QtQuick

QtObject {
    // --- Backgrounds ---
    readonly property color bg:          "#1e1e1e"
    readonly property color bgPanel:     "#2b2b2b"
    readonly property color bgHeader:    "#333333"
    readonly property color bgGutter:    "#1a1a1a"
    readonly property color bgButton:    "#3a3a3a"
    readonly property color bgButtonHov: "#555"
    readonly property color bgButtonPrs: "#666"
    readonly property color bgCard:      "#2f2f2f"
    readonly property color bgCardHov:   "#383838"
    readonly property color bgSelection: "#264f78"
    readonly property color bgActive:    "#3d6a99"
    readonly property color bgFooter:    "#252525"

    // --- Borders & Separators ---
    readonly property color border:      "#444"
    readonly property color borderHover: "#555"
    readonly property color borderFocus: accent

    // --- Text ---
    readonly property color textPrimary:     "#ddd"
    readonly property color textSecondary:   "#aaa"
    readonly property color textMuted:       "#888"
    readonly property color textDisabled:    "#666"
    readonly property color textWhite:       "#fff"
    readonly property color textBright:      "#eee"
    readonly property color textPlaceholder: "#666"

    // --- Accent ---
    readonly property color accent:           "#5b9bd5"
    readonly property color accentGreen:      "#4caf50"
    readonly property color accentGreenLight: "#a5d6a7"
    readonly property color accentGold:       "#e0c060"
    readonly property color accentOrange:     "#ff9800"
    readonly property color accentRed:        "#e06060"
    readonly property color accentPurple:     "#8888cc"

    // --- Memory view object colors (indexed 0-7) ---
    // 0=VALUE, 1=CURVE, 2=MAP, 3=CUBOID+, 4=ASCII, 5=VAL_BLK, 6=MEASUREMENT, 7=AXIS_PTS
    readonly property var memoryColors: [
        "#5b9bd5",  // 0 VALUE - blue
        "#26a69a",  // 1 CURVE - teal
        "#8888cc",  // 2 MAP - purple
        "#5c6bc0",  // 3 CUBOID+ - indigo
        "#ff9800",  // 4 ASCII - orange
        "#00bcd4",  // 5 VAL_BLK - cyan
        "#4caf50",  // 6 MEASUREMENT - green
        "#e0c060"   // 7 AXIS_PTS - yellow/gold
    ]
    readonly property color memoryUnoccupied: "#333"
    readonly property color memoryHighlight: "#fff"

    // --- Fonts ---
    readonly property string fontMono: "Consolas"
    readonly property int fontSizeS:  10
    readonly property int fontSizeM:  12
    readonly property int fontSizeL:  13
    readonly property int fontSizeXS: 11

    // --- Spacing ---
    readonly property int sp4:  4
    readonly property int sp8:  8
    readonly property int sp12: 12
    readonly property int sp16: 16

    // --- Sizes ---
    readonly property int headerHeight: 36
    readonly property int radius: 3
    readonly property int treeIndent: 14
    readonly property int tabWidthMin: 80
    readonly property int tabWidthMax: 220
}
