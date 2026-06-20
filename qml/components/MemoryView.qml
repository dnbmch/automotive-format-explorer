import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."
import ExplorerApp

// Memory View — hex grid visualization of ECU memory map.
// Uses C++ MemoryGridItem (QQuickPaintedItem) for fast rendering.

Item {
    id: memView

    required property var mapModel  // MemoryMapModel instance

    // Emitted when user clicks an object in the grid. Parent wires this
    // to AppController for tree/detail selection.
    signal objectClicked(int objectIndex)
    signal nodeKeyClicked(var nodeKey)

    readonly property int bpr: mapModel ? mapModel.bytesPerRow : 16

    function scrollToNodeKey(nodeKey) {
        if (!mapModel) return
        let seg = mapModel.segmentIndexForNodeKey(nodeKey)
        if (seg >= 0 && seg !== mapModel.currentSegment)
            mapModel.currentSegment = seg
        let objIdx = mapModel.objectIndexForNodeKey(nodeKey)
        if (objIdx < 0) return
        let addr = mapModel.objectAddress(objIdx)

        let row = gridItem.rowForAddress(addr)
        let rh = gridItem.cellSize + gridItem.cellGap
        // Center the object vertically.
        gridItem.scrollY = Math.max(0, row * rh - gridItem.height / 2)
        gridItem.highlightObject(objIdx)
    }

    readonly property var legendLabels: [
        "VALUE", "CURVE", "MAP", "CUBOID+", "ASCII", "VAL_BLK", "MEASUREMENT", "AXIS_PTS"
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- Toolbar: segment selector + jump-to-address ---
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: Theme.bgHeader

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 8

                ComboBox {
                    id: segmentCombo
                    visible: mapModel && mapModel.segmentCount > 1
                    Layout.preferredWidth: 400
                    model: {
                        if (!mapModel) return []
                        let items = []
                        for (let i = 0; i < mapModel.segmentCount; ++i)
                            items.push(mapModel.segmentLabel(i))
                        return items
                    }
                    currentIndex: mapModel ? mapModel.currentSegment : 0
                    onCurrentIndexChanged: {
                        if (mapModel && currentIndex >= 0)
                            mapModel.currentSegment = currentIndex
                    }
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeS
                    palette.button: Theme.bgButton
                    palette.buttonText: Theme.textPrimary
                    palette.window: Theme.bgPanel
                    palette.windowText: Theme.textPrimary
                }

                Label {
                    visible: mapModel && mapModel.segmentCount === 1
                    text: mapModel ? mapModel.segmentLabel(0) : ""
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeS
                    color: Theme.textSecondary
                }

                Item { Layout.fillWidth: true }

                Label {
                    text: "Go to:"
                    font.pixelSize: Theme.fontSizeS
                    color: Theme.textSecondary
                }
                TextField {
                    id: jumpField
                    Layout.preferredWidth: 100
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeS
                    placeholderText: "0x..."
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textPlaceholder
                    background: Rectangle {
                        color: Theme.bgButton
                        border.color: jumpField.activeFocus ? Theme.borderFocus : Theme.border
                        border.width: 1
                        radius: Theme.radius
                    }
                    onAccepted: {
                        if (!gridItem.model) return
                        let addr = parseInt(text, 16)
                        if (!isNaN(addr)) {
                            let row = gridItem.rowForAddress(addr)
                            let rh = gridItem.cellSize + gridItem.cellGap
                            gridItem.scrollY = row * rh
                        }
                    }
                }

                Row {
                    spacing: 2
                    Repeater {
                        model: [8, 16, 32]
                        delegate: Rectangle {
                            required property int modelData
                            width: 28; height: 22
                            radius: Theme.radius
                            color: memView.bpr === modelData ? Theme.bgActive : Theme.bgButton
                            border.color: Theme.border
                            border.width: 1

                            Label {
                                anchors.centerIn: parent
                                text: modelData
                                font.pixelSize: Theme.fontSizeXS
                                color: memView.bpr === modelData ? Theme.textWhite : Theme.textSecondary
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { if (mapModel) mapModel.bytesPerRow = modelData }
                            }
                        }
                    }
                }
            }
        }

        // --- Grid area ---
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.bg
            clip: true

            MemoryGridItem {
                id: gridItem
                anchors.left: parent.left
                anchors.right: vbar.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom

                model: mapModel

                Component.onCompleted: {
                    setColors(Theme.memoryColors, Theme.memoryUnoccupied)
                }

                onObjectClicked: function(objectIndex) {
                    memView.objectClicked(objectIndex)
                }
                onNodeKeyClicked: function(nodeKey) {
                    memView.nodeKeyClicked(nodeKey)
                }
            }

            ScrollBar {
                id: vbar
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                orientation: Qt.Vertical
                policy: ScrollBar.AsNeeded
                size: gridItem.contentHeight > 0
                      ? Math.min(1.0, gridItem.height / gridItem.contentHeight)
                      : 1.0
                position: gridItem.contentHeight > 0
                          ? gridItem.scrollY / gridItem.contentHeight
                          : 0

                onPositionChanged: {
                    if (pressed) {
                        gridItem.scrollY = position * gridItem.contentHeight
                    }
                }

                background: Rectangle { color: "transparent" }
                contentItem: Rectangle {
                    implicitWidth: 6
                    radius: 3
                    color: parent.pressed ? Theme.bgButtonPrs
                         : parent.hovered ? Theme.bgButtonHov : Theme.borderHover
                }
            }

            // Tooltip driven by C++ hover tracking.
            Rectangle {
                id: tooltip
                visible: gridItem.hoveredObjectIndex >= 0
                x: Math.min(gridItem.mouseX + 16, parent.width - width - 4)
                y: Math.min(gridItem.mouseY + 16, parent.height - height - 4)
                width: tooltipLabel.implicitWidth + 16
                height: tooltipLabel.implicitHeight + 10
                radius: Theme.radius
                color: "#e0222222"
                border.color: Theme.border
                border.width: 1
                z: 10

                Label {
                    id: tooltipLabel
                    anchors.centerIn: parent
                    text: gridItem.hoveredTooltip
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeS
                    color: Theme.textBright
                    lineHeight: 1.3
                }
            }
        }

        // --- Legend ---
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            color: Theme.bgPanel

            Row {
                anchors.centerIn: parent
                spacing: 12

                Repeater {
                    model: Theme.memoryColors.length
                    delegate: Row {
                        required property int index
                        spacing: 4

                        Rectangle {
                            width: 10; height: 10
                            radius: 2
                            color: Theme.memoryColors[index]
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Label {
                            text: memView.legendLabels[index]
                            font.pixelSize: 9
                            color: Theme.textMuted
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
        }

        // --- Status bar ---
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            color: Theme.bgFooter

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8

                Label {
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeS
                    color: Theme.textMuted
                    text: {
                        if (!mapModel || mapModel.segmentCount === 0) return ""
                        let start = "0x" + Number(mapModel.viewStartAddress).toString(16).toUpperCase().padStart(8, '0')
                        let end = "0x" + Number(mapModel.viewEndAddress - 1).toString(16).toUpperCase().padStart(8, '0')
                        return start + " \u2014 " + end + "  |  " + mapModel.objectCount + " objects"
                    }
                }

                Item { Layout.fillWidth: true }

                Label {
                    visible: mapModel && mapModel.excludedMeasurementCount > 0
                    font.pixelSize: Theme.fontSizeS
                    color: Theme.textMuted
                    text: mapModel ? mapModel.excludedMeasurementCount + " measurements without ECU address (not shown)" : ""
                }
            }
        }
    }
}
