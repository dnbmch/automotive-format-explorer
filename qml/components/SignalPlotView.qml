import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."
import ExplorerApp

// Format-neutral, single-series center panel. The property and signal names
// intentionally match the existing center-panel Loader contract.
Item {
    id: plotView

    required property var mapModel // SignalPlotModel instance

    signal nodeKeyClicked(var nodeKey)

    // Tree selection already reaches the owning session before this hook. The
    // provider updates mapModel asynchronously; there is nothing to scroll.
    function scrollToNodeKey(nodeKey) {}

    function numberText(value) {
        if (!isFinite(value))
            return "\u2014"
        let magnitude = Math.abs(value)
        if (magnitude >= 1000000 || (magnitude > 0 && magnitude < 0.0001))
            return Number(value).toExponential(3)
        let text = Number(value).toPrecision(6)
        return text.indexOf(".") >= 0 ? text.replace(/0+$/, "").replace(/\.$/, "") : text
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: Theme.bgHeader

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 8
                spacing: 8

                Rectangle {
                    Layout.preferredWidth: 3
                    Layout.preferredHeight: 18
                    radius: 2
                    color: Theme.accent
                }

                Label {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 360
                    text: mapModel && mapModel.name.length > 0
                          ? mapModel.name : "Signal plot"
                    elide: Text.ElideRight
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeM
                    font.bold: true
                    color: Theme.textBright
                }

                Rectangle {
                    visible: mapModel && mapModel.unit.length > 0
                    Layout.preferredWidth: unitLabel.implicitWidth + 12
                    Layout.preferredHeight: 20
                    radius: 10
                    color: Theme.bgActive

                    Label {
                        id: unitLabel
                        anchors.centerIn: parent
                        text: mapModel ? mapModel.unit : ""
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeS
                        color: Theme.textWhite
                    }
                }

                Label {
                    visible: mapModel && mapModel.hasSeries && plotView.width >= 500
                    text: mapModel ? Number(mapModel.sampleCount).toLocaleString() + " samples" : ""
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeS
                    color: Theme.textMuted
                }

                Item { Layout.fillWidth: true }

                BusyIndicator {
                    visible: running
                    running: mapModel ? mapModel.busy : false
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    palette.dark: Theme.accent
                    palette.light: Theme.border
                }

                Rectangle {
                    id: resetButton
                    Layout.preferredWidth: 78
                    Layout.preferredHeight: 24
                    radius: Theme.radius
                    color: resetMouse.pressed ? Theme.bgButtonPrs
                         : resetMouse.containsMouse ? Theme.bgButtonHov : Theme.bgButton
                    border.color: Theme.border
                    opacity: mapModel && mapModel.hasSeries ? 1.0 : 0.45

                    Label {
                        anchors.centerIn: parent
                        text: "Reset view"
                        font.pixelSize: Theme.fontSizeS
                        color: Theme.textPrimary
                    }

                    MouseArea {
                        id: resetMouse
                        anchors.fill: parent
                        enabled: mapModel && mapModel.hasSeries
                        hoverEnabled: true
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: plotItem.resetZoom()
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.bg
            clip: true

            SignalPlotItem {
                id: plotItem
                anchors.fill: parent
                model: mapModel

                Component.onCompleted: setColors(
                    Theme.bg,
                    Theme.border,
                    Theme.textMuted,
                    Theme.accent,
                    Theme.accentGold
                )

                HoverHandler {
                    cursorShape: plotItem.panning ? Qt.ClosedHandCursor : Qt.CrossCursor
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            color: Theme.bgFooter

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 9
                anchors.rightMargin: 9
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeS
                    color: mapModel && mapModel.cursorVisible
                           ? Theme.textSecondary : Theme.textMuted
                    text: {
                        if (!mapModel)
                            return ""
                        if (mapModel.busy)
                            return "Preparing samples\u2026"
                        if (mapModel.cursorVisible) {
                            let domainName = mapModel.domainName.length > 0
                                           ? mapModel.domainName : "Domain"
                            let domainSuffix = mapModel.domainUnit.length > 0
                                             ? " " + mapModel.domainUnit : ""
                            let suffix = mapModel.unit.length > 0 ? " " + mapModel.unit : ""
                            return domainName + " = " + plotView.numberText(mapModel.cursorTime)
                                 + domainSuffix + "  \u00b7  "
                                 + plotView.numberText(mapModel.cursorValue) + suffix
                                 + "  \u00b7  sample " + (Number(mapModel.cursorIndex) + 1)
                        }
                        if (mapModel.hasSeries) {
                            let domainName = mapModel.domainName.length > 0
                                           ? mapModel.domainName : "Domain"
                            let domainSuffix = mapModel.domainUnit.length > 0
                                             ? " " + mapModel.domainUnit : ""
                            return domainName + " window  "
                                 + plotView.numberText(mapModel.viewStart) + " \u2014 "
                                 + plotView.numberText(mapModel.viewEnd) + domainSuffix
                        }
                        if (mapModel.placeholderText.length > 0)
                            return mapModel.placeholderText
                        if (mapModel.name.length > 0)
                            return "No samples available"
                        return "Choose a signal in the tree to begin"
                    }
                }

                Label {
                    visible: mapModel && mapModel.hasSeries && !mapModel.busy
                             && plotView.width >= 620
                    text: "Wheel: zoom  \u00b7  Drag: pan  \u00b7  Double-click: reset"
                    font.pixelSize: Theme.fontSizeS
                    color: Theme.textDisabled
                }
            }
        }
    }
}
