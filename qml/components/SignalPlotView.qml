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

    readonly property int plotState: mapModel ? mapModel.plotState : SignalPlotModel.NoSignal
    readonly property bool hasSamples: mapModel ? mapModel.hasSamples : false
    readonly property color noteColor: plotState === SignalPlotModel.Failed ? Theme.accentRed
                                     : plotState === SignalPlotModel.Refused ? Theme.accentOrange
                                     : Theme.textMuted

    function numberText(value) {
        if (!isFinite(value))
            return "—"
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

                // The sample count where it fits. A source that ended early
                // says so at every width, and how much of it arrived where
                // that fits.
                Label {
                    readonly property bool incomplete: mapModel ? mapModel.incomplete : false
                    readonly property bool wide: plotView.width >= 500
                    visible: mapModel && mapModel.countText.length > 0 && (wide || incomplete)
                    text: !mapModel ? "" : !incomplete ? mapModel.countText
                        : wide ? "Incomplete: " + mapModel.countText : "Incomplete"
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeS
                    color: incomplete ? Theme.accentOrange : Theme.textMuted
                }

                // Whether the view shows every sample or the range of each column.
                Rectangle {
                    visible: plotView.hasSamples && plotView.width >= 420
                    Layout.preferredWidth: modeLabel.implicitWidth + 12
                    Layout.preferredHeight: 18
                    radius: 9
                    color: "transparent"
                    border.color: plotView.plotState === SignalPlotModel.Detail ? Theme.accentGreen
                                                                                 : Theme.border

                    Label {
                        id: modeLabel
                        anchors.centerIn: parent
                        text: plotView.plotState === SignalPlotModel.Detail ? "Exact samples"
                                                                             : "Overview"
                        font.pixelSize: Theme.fontSizeS
                        color: Theme.textSecondary
                    }
                }

                Item { Layout.fillWidth: true }

                Label {
                    visible: mapModel && mapModel.busy && mapModel.progress >= 0
                    text: mapModel ? Math.round(mapModel.progress * 100) + "%" : ""
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeS
                    color: Theme.textMuted
                }

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
                    opacity: plotView.hasSamples ? 1.0 : 0.45

                    Label {
                        anchors.centerIn: parent
                        text: "Reset view"
                        font.pixelSize: Theme.fontSizeS
                        color: Theme.textPrimary
                    }

                    MouseArea {
                        id: resetMouse
                        anchors.fill: parent
                        enabled: plotView.hasSamples
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

                // The hovered sample or columns; else the view with any note on
                // why it is not exact; else why nothing is plotted.
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeS
                    color: mapModel && mapModel.cursorVisible ? Theme.textSecondary
                                                              : plotView.noteColor
                    text: {
                        if (!mapModel)
                            return ""
                        if (mapModel.cursorVisible)
                            return mapModel.cursorText
                        let note = mapModel.message
                        if (plotView.hasSamples) {
                            let domainName = mapModel.domainName.length > 0
                                           ? mapModel.domainName : "Domain"
                            let domainSuffix = mapModel.domainUnit.length > 0
                                             ? " " + mapModel.domainUnit : ""
                            let range = domainName + " window  "
                                      + plotView.numberText(mapModel.viewStart) + " — "
                                      + plotView.numberText(mapModel.viewEnd) + domainSuffix
                            return note.length > 0 ? range + "  ·  " + note : range
                        }
                        if (note.length > 0)
                            return note
                        return "Choose a signal in the tree to begin"
                    }
                }

                Label {
                    visible: plotView.hasSamples && plotView.width >= 620
                    text: "Wheel: zoom  ·  Drag: pan  ·  Double-click: reset"
                    font.pixelSize: Theme.fontSizeS
                    color: Theme.textDisabled
                }
            }
        }
    }
}
