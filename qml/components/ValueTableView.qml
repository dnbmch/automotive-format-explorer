import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."
import ExplorerApp

// The value table of a text or array channel: a page of texts with their
// sample indices and times, or one sample of an array as rows × columns with
// its axes as headers. The property and signal names match the center-panel
// Loader contract.
Item {
    id: tableView

    required property var mapModel // ValueTableModel instance

    signal nodeKeyClicked(var nodeKey)

    // Tree selection already reaches the owning session before this hook; the
    // table has nothing to scroll to.
    function scrollToNodeKey(nodeKey) {}

    readonly property bool isArray: mapModel ? mapModel.kind === ValueTableModel.Array : false
    readonly property int tableState: mapModel ? mapModel.tableState : ValueTableModel.NoChannel
    readonly property color noteColor: tableState === ValueTableModel.Failed ? Theme.accentRed
                                     : tableState === ValueTableModel.Refused ? Theme.accentOrange
                                     : Theme.textMuted
    // The wanted page's first sample, or the wanted array sample.
    readonly property string position: !mapModel ? ""
                                     : String(isArray ? mapModel.sample : mapModel.firstRow)
    readonly property int timeColumnWidth: 120
    readonly property int rowHeight: 22

    // The column headers' font, which an array column's width also fits.
    FontMetrics {
        id: headerMetrics
        font.family: Theme.fontMono
        font.pixelSize: Theme.fontSizeS
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // The channel: name, role and unit chips, sample count, the array
        // sample's time, and progress.
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
                    text: mapModel && mapModel.name.length > 0 ? mapModel.name : "Value table"
                    elide: Text.ElideRight
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeM
                    font.bold: true
                    color: Theme.textBright
                }

                Rectangle {
                    visible: mapModel && mapModel.role.length > 0
                    Layout.preferredWidth: roleLabel.implicitWidth + 12
                    Layout.preferredHeight: 20
                    radius: 10
                    color: "transparent"
                    border.color: Theme.border

                    Label {
                        id: roleLabel
                        anchors.centerIn: parent
                        text: mapModel ? mapModel.role : ""
                        font.pixelSize: Theme.fontSizeS
                        color: Theme.textSecondary
                    }
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
                    visible: mapModel && mapModel.countText.length > 0 && tableView.width >= 500
                    text: mapModel ? mapModel.countText : ""
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeS
                    color: Theme.textMuted
                }

                Label {
                    visible: tableView.isArray && mapModel.timeText.length > 0
                             && tableView.width >= 420
                    text: mapModel ? "t = " + mapModel.timeText : ""
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeS
                    color: Theme.textSecondary
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
            }
        }

        // Where the table stands: a sample field with the previous and next
        // page (text) or sample (array), and one index per outer dimension.
        // The controls wrap onto a second line in a narrow pane.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: controls.implicitHeight + 8
            color: Theme.bgPanel
            visible: tableView.tableState !== ValueTableModel.NoChannel

            Flow {
                id: controls
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 10
                anchors.rightMargin: 8
                spacing: 6

                Label {
                    height: 24
                    verticalAlignment: Text.AlignVCenter
                    text: "Sample"
                    font.pixelSize: Theme.fontSizeS
                    color: Theme.textSecondary
                }

                TextField {
                    id: sampleField
                    width: 88
                    height: 24
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeS
                    color: Theme.textPrimary
                    selectByMouse: true
                    validator: RegularExpressionValidator { regularExpression: /[0-9]{0,20}/ }
                    text: tableView.position
                    ToolTip.text: tableView.isArray ? "Show this sample" : "Show the page holding this sample"
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    onEditingFinished: {
                        if (mapModel && text.length > 0)
                            mapModel.showSample(Number(text))
                        text = Qt.binding(function() { return tableView.position })
                    }

                    background: Rectangle {
                        color: Theme.bg
                        radius: Theme.radius
                        border.color: sampleField.activeFocus ? Theme.accent : Theme.border
                    }
                }

                Repeater {
                    model: [
                        { glyph: "‹", next: false },
                        { glyph: "›", next: true }
                    ]

                    delegate: Rectangle {
                        id: stepButton
                        required property var modelData
                        readonly property bool enabledStep: mapModel
                            ? (modelData.next ? mapModel.hasNext : mapModel.hasPrevious) : false
                        width: 24
                        height: 24
                        radius: Theme.radius
                        color: stepMouse.pressed ? Theme.bgButtonPrs
                             : stepMouse.containsMouse ? Theme.bgButtonHov : Theme.bgButton
                        border.color: Theme.border
                        opacity: enabledStep ? 1.0 : 0.45

                        Label {
                            anchors.centerIn: parent
                            text: stepButton.modelData.glyph
                            font.pixelSize: Theme.fontSizeL
                            color: Theme.textPrimary
                        }

                        ToolTip.text: (stepButton.modelData.next ? "Next " : "Previous ")
                                      + (tableView.isArray ? "sample" : "page")
                        ToolTip.visible: stepMouse.containsMouse
                        ToolTip.delay: 400

                        MouseArea {
                            id: stepMouse
                            anchors.fill: parent
                            enabled: stepButton.enabledStep
                            hoverEnabled: true
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: stepButton.modelData.next ? mapModel.nextPage()
                                                                 : mapModel.previousPage()
                        }
                    }
                }

                Label {
                    visible: sliceRepeater.count > 0
                    height: 24
                    leftPadding: 8
                    verticalAlignment: Text.AlignVCenter
                    text: "Slice"
                    font.pixelSize: Theme.fontSizeS
                    color: Theme.textSecondary
                }

                Repeater {
                    id: sliceRepeater
                    model: tableView.isArray ? mapModel.slice.length : 0

                    delegate: SpinBox {
                        required property int index
                        width: 88
                        height: 24
                        from: 0
                        to: Math.max(0, mapModel.dimensions[index] - 1)
                        value: mapModel.slice[index]
                        editable: true
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeS
                        palette.base: Theme.bg
                        palette.text: Theme.textPrimary
                        palette.button: Theme.bgButton
                        palette.buttonText: Theme.textPrimary
                        palette.mid: Theme.border
                        palette.dark: Theme.border
                        palette.highlight: Theme.accent
                        ToolTip.text: "Index of dimension " + (index + 1) + " of "
                                      + mapModel.dimensions.length
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        onValueModified: mapModel.setSlice(index, value)
                    }
                }
            }
        }

        // The cells, the column headers above them, the row headers beside
        // them and the corner naming the two axes.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.bg

            GridLayout {
                anchors.fill: parent
                columns: 2
                rowSpacing: 0
                columnSpacing: 0

                Rectangle {
                    Layout.preferredWidth: rowHeader.width
                    Layout.preferredHeight: columnHeader.height
                    color: Theme.bgHeader

                    Label {
                        id: cornerLabel
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        verticalAlignment: Text.AlignVCenter
                        text: mapModel ? mapModel.cornerText : ""
                        elide: Text.ElideRight
                        font.pixelSize: Theme.fontSizeS
                        color: Theme.textMuted
                    }
                }

                HorizontalHeaderView {
                    id: columnHeader
                    Layout.fillWidth: true
                    syncView: table
                    clip: true

                    delegate: Rectangle {
                        id: columnCell
                        required property var model
                        implicitWidth: 64
                        implicitHeight: tableView.rowHeight + 2
                        color: Theme.bgHeader
                        border.color: Theme.border

                        Label {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: tableView.isArray ? Text.AlignRight : Text.AlignLeft
                            text: columnCell.model.display ?? ""
                            elide: Text.ElideRight
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSizeS
                            color: Theme.textSecondary
                        }
                    }
                }

                VerticalHeaderView {
                    id: rowHeader
                    Layout.fillHeight: true
                    syncView: table
                    clip: true

                    delegate: Rectangle {
                        id: rowCell
                        required property var model
                        // Wide enough for the corner's axis names, within reason.
                        implicitWidth: Math.max(48, rowLabel.implicitWidth + 12,
                                                Math.min(160, cornerLabel.implicitWidth + 12))
                        implicitHeight: tableView.rowHeight
                        color: Theme.bgHeader

                        Label {
                            id: rowLabel
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            text: rowCell.model.display ?? ""
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSizeS
                            color: Theme.textSecondary
                        }
                    }
                }

                TableView {
                    id: table
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: tableView.mapModel

                    // Text: Time at a fixed width and Text filling the rest; array
                    // values as wide as their cells need.
                    columnWidthProvider: function(column) {
                        if (tableView.isArray)
                            return -1
                        if (table.columns > 1 && column === 0)
                            return tableView.timeColumnWidth
                        return Math.max(120, table.width - (table.columns > 1 ? tableView.timeColumnWidth : 0))
                    }
                    rowHeightProvider: function(row) { return tableView.rowHeight }
                    onWidthChanged: forceLayout()

                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Rectangle {
                        id: valueCell
                        required property string display
                        required property int row
                        required property int column
                        // An array column fits its values and its header's label.
                        implicitWidth: Math.max(72, valueLabel.implicitWidth + 12,
                                                tableView.isArray
                                                ? headerMetrics.advanceWidth(String(
                                                      tableView.mapModel.headerData(
                                                          column, Qt.Horizontal))) + 12
                                                : 0)
                        implicitHeight: tableView.rowHeight
                        color: row % 2 === 0 ? Theme.bg : Theme.bgCard

                        Label {
                            id: valueLabel
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: tableView.isArray ? Text.AlignRight : Text.AlignLeft
                            text: valueCell.display
                            elide: Text.ElideRight
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSizeM
                            color: Theme.textPrimary
                        }
                    }
                }
            }
        }

        // The note on why nothing, or not everything, shows; else the rows a
        // text page shows and how many of them were invalid.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            color: Theme.bgFooter

            Label {
                anchors.fill: parent
                anchors.leftMargin: 9
                anchors.rightMargin: 9
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSizeS
                color: mapModel && mapModel.message.length > 0 ? tableView.noteColor
                                                               : Theme.textSecondary
                text: {
                    if (!mapModel)
                        return ""
                    if (mapModel.message.length > 0)
                        return mapModel.message
                    if (mapModel.rangeText.length === 0)
                        return ""
                    return mapModel.invalidCount > 0
                           ? mapModel.rangeText + "  ·  " + mapModel.invalidCount + " invalid"
                           : mapModel.rangeText
                }
            }
        }
    }
}
