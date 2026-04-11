import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExplorerApp

Rectangle {
    id: navPanel
    color: Theme.bgPanel

    property alias treeModel: treeView.model
    property bool hasContent: treeView.rows > 0

    signal nodeSelected(var nodeKey)
    signal collapseRequested()

    function selectAndScrollTo(nodeKey) {
        if (!treeView.model) return
        let modelIdx = treeView.model.indexForNodeKey(nodeKey)
        if (!modelIdx.valid) return

        // Expand all ancestors so the node is visible.
        let parentIdx = treeView.model.parent(modelIdx)
        let toExpand = []
        while (parentIdx.valid) {
            toExpand.push(parentIdx)
            parentIdx = treeView.model.parent(parentIdx)
        }
        for (let i = toExpand.length - 1; i >= 0; --i) {
            let row = treeView.rowAtIndex(toExpand[i])
            if (row >= 0 && !treeView.isExpanded(row))
                treeView.expand(row)
        }

        // Force layout so row indices are up to date after expanding.
        treeView.forceLayout()

        // Select and scroll.
        let row = treeView.rowAtIndex(modelIdx)
        if (row >= 0) {
            treeView.selectionModel.setCurrentIndex(
                treeView.index(row, 0),
                ItemSelectionModel.ClearAndSelect)
            treeView.positionViewAtRow(row, Qt.AlignVCenter)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.headerHeight
            color: Theme.bgHeader

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 6
                spacing: 4

                Label {
                    text: "EXPLORER"
                    font.pixelSize: Theme.fontSizeXS
                    font.bold: true
                    font.letterSpacing: 1.2
                    color: Theme.textSecondary
                    Layout.fillWidth: true
                }

                // Expand all
                Rectangle {
                    width: 24; height: 24
                    radius: Theme.radius
                    color: expandMa.containsMouse ? Theme.bgButtonHov : "transparent"
                    visible: navPanel.hasContent
                    ToolTip.text: "Expand all"
                    ToolTip.visible: expandMa.containsMouse
                    ToolTip.delay: 400

                    Label {
                        anchors.centerIn: parent
                        text: "\u229E"
                        font.pixelSize: 14
                        color: Theme.textSecondary
                    }
                    MouseArea {
                        id: expandMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: treeView.expandRecursively()
                    }
                }

                // Collapse all
                Rectangle {
                    width: 24; height: 24
                    radius: Theme.radius
                    color: collapseMa.containsMouse ? Theme.bgButtonHov : "transparent"
                    visible: navPanel.hasContent
                    ToolTip.text: "Collapse all"
                    ToolTip.visible: collapseMa.containsMouse
                    ToolTip.delay: 400

                    Label {
                        anchors.centerIn: parent
                        text: "\u229F"
                        font.pixelSize: 14
                        color: Theme.textSecondary
                    }
                    MouseArea {
                        id: collapseMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: treeView.collapseRecursively()
                    }
                }

                // Collapse pane
                Rectangle {
                    width: 24; height: 24
                    radius: Theme.radius
                    color: collapsePaneMa.containsMouse ? Theme.bgButtonHov : "transparent"
                    ToolTip.text: "Hide panel"
                    ToolTip.visible: collapsePaneMa.containsMouse
                    ToolTip.delay: 400

                    Label {
                        anchors.centerIn: parent
                        text: "\u25C0"
                        font.pixelSize: 10
                        color: Theme.textSecondary
                    }
                    MouseArea {
                        id: collapsePaneMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: navPanel.collapseRequested()
                    }
                }
            }
        }

        // Thin accent strip under header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 2
            color: Theme.accent
            opacity: 0.3
        }

        // Tree view area
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Empty state
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 8
                visible: !navPanel.hasContent

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: "No file open"
                    font.pixelSize: Theme.fontSizeL
                    color: Theme.textMuted
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Open a supported file to\npopulate the tree."
                    font.pixelSize: Theme.fontSizeXS
                    color: Theme.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    lineHeight: 1.3
                }
            }

            TreeView {
                id: treeView
                anchors.fill: parent
                visible: navPanel.hasContent
                clip: true
                columnWidthProvider: function(column) {
                    return treeView.width
                }
                onWidthChanged: treeView.forceLayout()

                ScrollBar.vertical: ScrollBar {
                    id: treeVScroll
                    policy: ScrollBar.AsNeeded
                    background: Rectangle { color: "transparent" }
                    contentItem: Rectangle {
                        implicitWidth: 6
                        radius: 3
                        color: parent.pressed ? Theme.bgButtonPrs
                             : parent.hovered ? Theme.bgButtonHov : Theme.borderHover
                    }
                }

                selectionModel: ItemSelectionModel {
                    model: treeView.model
                }

                delegate: TreeViewDelegate {
                    id: treeDelegate
                    implicitHeight: 26
                    indentation: Theme.treeIndent
                    leftMargin: 4

                    // Hide built-in indicator
                    indicator: Item {}

                    required property string title
                    required property string subtitle
                    required property string iconKey
                    required property bool selectable
                    required property var nodeKey
                    required property int semanticKind

                    contentItem: RowLayout {
                        spacing: 5

                        // Expand indicator for non-leaf nodes (wider hit area)
                        Item {
                            Layout.preferredWidth: 16
                            Layout.fillHeight: true

                            Label {
                                anchors.centerIn: parent
                                visible: treeDelegate.hasChildren
                                text: treeDelegate.expanded ? "\u25BE" : "\u25B8"
                                font.pixelSize: Theme.fontSizeS
                                color: Theme.textSecondary
                            }

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -4
                                visible: treeDelegate.hasChildren
                                cursorShape: Qt.PointingHandCursor
                                onClicked: treeView.toggleExpanded(treeDelegate.row)
                            }
                        }

                        // Semantic icon/glyph
                        Label {
                            text: {
                                switch (treeDelegate.semanticKind) {
                                case 0: return "\u25A0"  // Root — filled square
                                case 1: return "\u25AB"  // Section — small square
                                case 2: return "\u25CF"  // Entity — filled circle
                                case 3: return "\u25CB"  // Attribute — circle
                                case 4: return "\u25C6"  // Diagnostic — diamond
                                default: return "\u25CB"
                                }
                            }
                            font.pixelSize: treeDelegate.semanticKind === 0 ? 11 : 9
                            color: {
                                switch (treeDelegate.semanticKind) {
                                case 0: return Theme.accent        // Root
                                case 1: return Theme.textMuted     // Section
                                case 2: return Theme.textSecondary // Entity
                                case 3: return Theme.textMuted     // Attribute
                                case 4: return Theme.accentGold    // Diagnostic
                                default: return Theme.textSecondary
                                }
                            }
                            Layout.preferredWidth: 12
                            horizontalAlignment: Text.AlignHCenter
                        }

                        // Label
                        Label {
                            text: treeDelegate.subtitle.length > 0
                                ? treeDelegate.title + "  " + treeDelegate.subtitle
                                : treeDelegate.title
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            font.bold: treeDelegate.semanticKind === 0
                            font.pixelSize: Theme.fontSizeM
                            color: treeDelegate.current ? Theme.textWhite : Theme.textPrimary
                        }

                        // Scrollbar clearance
                        Item {
                            Layout.preferredWidth: treeVScroll.visible ? (treeVScroll.width + 4) : 0
                        }
                    }

                    background: Rectangle {
                        color: treeDelegate.current ? Theme.bgActive
                             : treeDelegate.hovered ? Theme.bgCardHov
                             : "transparent"
                    }

                    TapHandler {
                        onSingleTapped: {
                            if (treeDelegate.selectable) {
                                treeView.selectionModel.setCurrentIndex(
                                    treeView.index(treeDelegate.row, 0),
                                    ItemSelectionModel.ClearAndSelect)
                                navPanel.nodeSelected(treeDelegate.nodeKey)
                            } else if (treeDelegate.hasChildren) {
                                treeView.toggleExpanded(treeDelegate.row)
                            }
                        }
                        onDoubleTapped: {
                            if (treeDelegate.hasChildren) {
                                treeView.toggleExpanded(treeDelegate.row)
                            }
                        }
                    }
                }
            }
        }

        // Help bar at bottom
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            color: Theme.bgHeader
            visible: navPanel.hasContent

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 6
                spacing: 4

                Label {
                    text: "Double-click to expand/collapse"
                    font.pixelSize: 10
                    color: Theme.textMuted
                    Layout.fillWidth: true
                }

                Rectangle {
                    width: 18; height: 18
                    radius: 9
                    color: helpMa.containsMouse ? Theme.bgButtonHov : Theme.bgButton
                    ToolTip.text: "Help & shortcuts"
                    ToolTip.visible: helpMa.containsMouse
                    ToolTip.delay: 300

                    Label {
                        anchors.centerIn: parent
                        text: "?"
                        font.pixelSize: 11
                        font.bold: true
                        color: Theme.textSecondary
                    }
                    MouseArea {
                        id: helpMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: helpPopup.open()
                    }
                }
            }
        }
    }

    // Help popup / manual
    Popup {
        id: helpPopup
        anchors.centerIn: parent
        width: Math.min(navPanel.width - 32, 340)
        height: contentColumn.implicitHeight + 32
        modal: true
        padding: 16

        background: Rectangle {
            color: Theme.bgPanel
            border.color: Theme.border
            border.width: 1
            radius: Theme.radius * 2
        }

        ColumnLayout {
            id: contentColumn
            anchors.fill: parent
            spacing: 12

            Label {
                text: "Help & Keyboard Shortcuts"
                font.pixelSize: Theme.fontSizeL
                font.bold: true
                color: Theme.textWhite
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.border
            }

            Label {
                text: "Navigation"
                font.pixelSize: Theme.fontSizeM
                font.bold: true
                color: Theme.accent
            }

            Label {
                text: "• Click a tree item to select it\n"
                    + "• Double-click to expand/collapse\n"
                    + "• Click the \u25B8 chevron to expand/collapse"
                font.pixelSize: Theme.fontSizeXS
                color: Theme.textSecondary
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                lineHeight: 1.4
            }

            Label {
                text: "Keyboard Shortcuts"
                font.pixelSize: Theme.fontSizeM
                font.bold: true
                color: Theme.accent
            }

            GridLayout {
                columns: 2
                columnSpacing: 16
                rowSpacing: 4
                Layout.fillWidth: true

                Label { text: "Ctrl+O";             font.pixelSize: Theme.fontSizeXS; font.family: Theme.fontMono; color: Theme.accentGold }
                Label { text: "Open file";           font.pixelSize: Theme.fontSizeXS; color: Theme.textSecondary }
                Label { text: "Ctrl+W";              font.pixelSize: Theme.fontSizeXS; font.family: Theme.fontMono; color: Theme.accentGold }
                Label { text: "Close tab";           font.pixelSize: Theme.fontSizeXS; color: Theme.textSecondary }
                Label { text: "Ctrl+Tab";            font.pixelSize: Theme.fontSizeXS; font.family: Theme.fontMono; color: Theme.accentGold }
                Label { text: "Next tab";            font.pixelSize: Theme.fontSizeXS; color: Theme.textSecondary }
                Label { text: "Ctrl+Shift+Tab";      font.pixelSize: Theme.fontSizeXS; font.family: Theme.fontMono; color: Theme.accentGold }
                Label { text: "Previous tab";        font.pixelSize: Theme.fontSizeXS; color: Theme.textSecondary }
                Label { text: "Ctrl+Alt+B";          font.pixelSize: Theme.fontSizeXS; font.family: Theme.fontMono; color: Theme.accentGold }
                Label { text: "Toggle sidebar";      font.pixelSize: Theme.fontSizeXS; color: Theme.textSecondary }
            }

            Label {
                text: "Header Buttons"
                font.pixelSize: Theme.fontSizeM
                font.bold: true
                color: Theme.accent
            }

            Label {
                text: "• \u229E  Expand all tree nodes\n"
                    + "• \u229F  Collapse all tree nodes\n"
                    + "• \u25C0  Hide sidebar panel"
                font.pixelSize: Theme.fontSizeXS
                color: Theme.textSecondary
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                lineHeight: 1.4
            }

            Item { Layout.preferredHeight: 4 }

            Button {
                text: "Close"
                Layout.alignment: Qt.AlignRight
                onClicked: helpPopup.close()

                background: Rectangle {
                    implicitWidth: 64
                    implicitHeight: 28
                    radius: Theme.radius
                    color: parent.hovered ? Theme.bgButtonHov : Theme.bgButton
                }

                contentItem: Label {
                    text: parent.text
                    font.pixelSize: Theme.fontSizeXS
                    color: Theme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
