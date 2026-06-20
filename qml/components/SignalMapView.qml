import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."
import ExplorerApp

// Signal Map View — bit-level visualization of CAN/LIN message payloads.
// Uses C++ SignalGridItem (QQuickPaintedItem) for rendering.

Item {
    id: sigView

    required property var mapModel  // SignalMapModel instance

    signal signalClicked(int signalIndex)
    signal nodeKeyClicked(var nodeKey)

    function scrollToNodeKey(nodeKey) {
        if (!mapModel) return

        let msgIdx = mapModel.messageIndexForNodeKey(nodeKey)
        if (msgIdx < 0) return

        if (msgIdx !== mapModel.currentMessage) {
            mapModel.currentMessage = msgIdx
            messageCombo.currentIndex = msgIdx
        }

        let sigIdx = mapModel.signalIndexForNodeKey(nodeKey)
        if (sigIdx >= 0) {
            gridItem.selectedSignalIndex = sigIdx
            if (!mapModel.isSignalVisible(sigIdx)) {
                let g = mapModel.muxGroupForSignal(sigIdx)
                mapModel.currentMuxGroup = g
                muxCombo.currentIndex = g + 1
            }
            gridItem.highlightSignal(sigIdx)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- Toolbar: message selector + mux group selector ---
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
                    id: messageCombo
                    Layout.fillWidth: true
                    Layout.maximumWidth: 500
                    model: {
                        if (!mapModel) return []
                        let items = []
                        for (let i = 0; i < mapModel.messageCount; ++i)
                            items.push(mapModel.messageLabel(i))
                        return items
                    }
                    currentIndex: mapModel ? mapModel.currentMessage : 0
                    onCurrentIndexChanged: {
                        if (mapModel && currentIndex >= 0)
                            mapModel.currentMessage = currentIndex
                    }
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeS
                    palette.button: Theme.bgButton
                    palette.buttonText: Theme.textPrimary
                    palette.window: Theme.bgPanel
                    palette.windowText: Theme.textPrimary
                }

                // Mux group selector — only visible for multiplexed messages.
                ComboBox {
                    id: muxCombo
                    visible: mapModel && mapModel.hasMux
                    Layout.preferredWidth: 120
                    model: {
                        if (!mapModel || !mapModel.hasMux) return []
                        let items = ["All"]
                        for (let i = 0; i < mapModel.muxGroupCount; ++i)
                            items.push(mapModel.muxGroupLabel(i))
                        return items
                    }
                    currentIndex: mapModel ? mapModel.currentMuxGroup + 1 : 0
                    onCurrentIndexChanged: {
                        if (mapModel)
                            mapModel.currentMuxGroup = currentIndex - 1
                    }
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeS
                    palette.button: Theme.bgButton
                    palette.buttonText: Theme.textPrimary
                    palette.window: Theme.bgPanel
                    palette.windowText: Theme.textPrimary
                }

                Item { Layout.fillWidth: true }
            }
        }

        // --- Grid area (scrollable for CAN FD) ---
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.bg
            clip: true

            Flickable {
                id: flickable
                anchors.left: parent.left
                anchors.right: vbar.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                contentWidth: width
                contentHeight: gridItem.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height

                SignalGridItem {
                    id: gridItem
                    width: flickable.width
                    height: Math.max(implicitHeight, 20)

                    model: mapModel

                    Component.onCompleted: {
                        setColors(Theme.signalColors, Theme.memoryUnoccupied)
                    }

                    onSignalClicked: function(signalIndex) {
                        sigView.signalClicked(signalIndex)
                    }
                    onNodeKeyClicked: function(nodeKey) {
                        sigView.nodeKeyClicked(nodeKey)
                    }
                }
            }

            ScrollBar {
                id: vbar
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                orientation: Qt.Vertical
                policy: flickable.contentHeight > flickable.height
                        ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                size: flickable.contentHeight > 0
                      ? Math.min(1.0, flickable.height / flickable.contentHeight)
                      : 1.0
                position: flickable.contentHeight > 0
                          ? flickable.contentY / flickable.contentHeight
                          : 0
                onPositionChanged: {
                    if (pressed)
                        flickable.contentY = position * flickable.contentHeight
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
                visible: gridItem.hoveredSignalIndex >= 0
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

        // --- Legend: dynamic signal names for current message (wrapping) ---
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: mapModel && mapModel.signalCount > 0 ? legendFlow.height + 8 : 0
            visible: mapModel && mapModel.signalCount > 0
            color: Theme.bgPanel

            Flow {
                id: legendFlow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Repeater {
                    model: sigView.mapModel ? sigView.mapModel.signalCount : 0
                    delegate: Row {
                        required property int index
                        spacing: 4
                        height: 16
                        visible: sigView.mapModel ? sigView.mapModel.isSignalVisible(index) : false

                        Rectangle {
                            width: 10; height: 10
                            radius: 2
                            color: {
                                if (!sigView.mapModel) return "gray"
                                let ci = sigView.mapModel.signalColorIndex(index)
                                return Theme.signalColors[ci % Theme.signalColors.length]
                            }
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Label {
                            text: {
                                if (!sigView.mapModel) return ""
                                let name = sigView.mapModel.signalName(index)
                                let muxType = sigView.mapModel.signalMuxType(index)
                                if (muxType === 1) return name + " [M]"
                                if (muxType === 2) return name + " [m]"
                                if (muxType === 3) return name + " [Mm]"
                                return name
                            }
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
                        if (!mapModel || mapModel.messageCount === 0) return ""
                        let dlc = mapModel.dlcBytes
                        let bits = mapModel.dlcBits
                        let used = mapModel.usedBits
                        let sigs = mapModel.hasMux ? mapModel.visibleSignalCount : mapModel.signalCount
                        let pct = bits > 0 ? Math.round(used / bits * 100) : 0
                        return "DLC: " + dlc + " bytes (" + bits + " bits)  |  " +
                               sigs + " signals  |  " +
                               used + "/" + bits + " bits used (" + pct + "%)"
                    }
                }

                Item { Layout.fillWidth: true }

                Label {
                    visible: mapModel && mapModel.messageSender.length > 0
                    font.pixelSize: Theme.fontSizeS
                    color: Theme.textMuted
                    text: mapModel ? "Sender: " + mapModel.messageSender : ""
                }
            }
        }
    }
}
