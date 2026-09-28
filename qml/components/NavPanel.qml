import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ExplorerApp

Rectangle {
    id: navPanel
    color: Theme.bgPanel

    property var tab: null          // the current DocumentTab
    // Source-backed content: stays true while a filter yields zero visible rows.
    readonly property bool hasSource: tab ? tab.treeModel.sourceModel.rowCount() > 0 : false

    // The tab whose navigation the view shows. It is null while a newly shown tab
    // waits for its restore: that view is not the tab's navigation, so it is never
    // saved over it.
    property var _appliedTab: null
    // Every tab or filter change starts a new generation, so a scheduled restore
    // of the current generation targets the tab still shown.
    property int _restoreGeneration: 0

    onTabChanged: {
        if (_appliedTab)
            _save(_appliedTab)
        _appliedTab = null
        const target = tab
        const generation = ++_restoreGeneration
        treeView.model = target ? target.treeModel : null
        searchField.text = target ? target.filterText : ""
        // The view lays out the new rows before their state can be applied.
        if (target)
            Qt.callLater(function() { _restore(target, generation) })
    }

    // A superseded restore does nothing, and so does one whose tab was destroyed
    // meanwhile: a destroyed tab reads no tree.
    function _restore(target, generation) {
        if (generation !== _restoreGeneration || !target.treeModel)
            return
        _apply(target)
        _appliedTab = target
    }

    function focusSearch() {
        searchField.forceActiveFocus()
        searchField.selectAll()
    }

    // Drives the tab's filter. Entering a filter saves the view as the tab's
    // navigation, which the tab keeps as the pre-filter snapshot; clearing it
    // applies the snapshot the tab gives back. While a restore is pending, the
    // tab's saved navigation already is the one to keep.
    function _applyFilter(text) {
        if (searchField.text !== text)
            searchField.text = text
        if (!tab || tab.filterText === text)
            return
        if (tab.filterText.length === 0 && _appliedTab === tab)
            _save(tab)
        ++_restoreGeneration
        tab.setFilterText(text)
        if (text.length > 0) {
            treeView.expandRecursively()
            treeView.forceLayout()
        } else {
            treeView.collapseRecursively()
            treeView.forceLayout()
            _apply(tab)
        }
        _appliedTab = tab
    }

    // Saves what the view shows: the keys of the expanded rows in row order, so
    // parents precede children, the current row's key and the scroll position.
    // The view's rows and scroll position follow its layout, brought up to date
    // first.
    function _save(target) {
        treeView.forceLayout()
        const model = treeView.model
        let keys = []
        for (let r = 0; r < treeView.rows; ++r) {
            if (treeView.isExpanded(r))
                keys.push(model.data(treeView.index(r, 0), model.nodeKeyRole))
        }
        const current = treeView.selectionModel.currentIndex
        target.saveNavigation(keys, current.valid ? model.data(current, model.nodeKeyRole) : 0,
                              treeView.contentY)
    }

    // Applies a tab's saved navigation to the view showing its tree. Every row,
    // category or entity, has a key, so each saved row is found again.
    function _apply(target) {
        const model = target.treeModel
        const keys = target.expandedKeys
        for (let i = 0; i < keys.length; ++i) {
            const row = treeView.rowAtIndex(model.indexForNodeKey(keys[i]))
            if (row >= 0 && !treeView.isExpanded(row)) {
                treeView.expand(row)
                treeView.forceLayout()
            }
        }
        // The saved model index itself, not its row: a current row hidden under a
        // collapsed parent is restored, and key 0 clears the selection.
        treeView.selectionModel.setCurrentIndex(model.indexForNodeKey(target.currentKey),
                                                ItemSelectionModel.ClearAndSelect)
        treeView.contentY = target.contentY
    }

    signal nodeSelected(var nodeKey)
    signal collapseRequested()
    signal openRequested()

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
            if (row >= 0 && !treeView.isExpanded(row)) {
                treeView.expand(row)
                treeView.forceLayout()
            }
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

                // Open file
                Rectangle {
                    width: 24; height: 24
                    radius: Theme.radius
                    color: openMa.containsMouse ? Theme.bgButtonHov : "transparent"
                    ToolTip.text: "Open file (Ctrl+O)"
                    ToolTip.visible: openMa.containsMouse
                    ToolTip.delay: 400

                    Label {
                        anchors.centerIn: parent
                        text: "\u2750"
                        font.pixelSize: 14
                        color: Theme.textSecondary
                    }
                    MouseArea {
                        id: openMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: navPanel.openRequested()
                    }
                }

                // Expand all
                Rectangle {
                    width: 24; height: 24
                    radius: Theme.radius
                    color: expandMa.containsMouse ? Theme.bgButtonHov : "transparent"
                    visible: navPanel.hasSource
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
                    visible: navPanel.hasSource
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

        // Filter bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: Theme.bgPanel
            visible: navPanel.hasSource

            TextField {
                id: searchField
                anchors.fill: parent
                anchors.margins: 5
                leftPadding: 8
                rightPadding: clearFilterButton.visible ? clearFilterButton.width + 10 : 8
                placeholderText: "Filter (Ctrl+F)"
                placeholderTextColor: Theme.textMuted
                font.pixelSize: Theme.fontSizeM
                color: Theme.textPrimary
                selectionColor: Theme.bgSelection
                selectedTextColor: Theme.textWhite

                onTextEdited: navPanel._applyFilter(text)
                Keys.onEscapePressed: {
                    if (text.length > 0)
                        navPanel._applyFilter("")
                    else
                        focus = false
                }

                background: Rectangle {
                    color: Theme.bg
                    radius: Theme.radius
                    border.color: searchField.activeFocus ? Theme.accent : Theme.border
                    border.width: 1
                }

                Rectangle {
                    id: clearFilterButton
                    anchors.right: parent.right
                    anchors.rightMargin: 5
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16; height: 16
                    radius: 8
                    visible: searchField.text.length > 0
                    color: clearFilterMa.containsMouse ? Theme.bgButtonHov : "transparent"

                    Label {
                        anchors.centerIn: parent
                        text: "✕"
                        font.pixelSize: 9
                        color: clearFilterMa.containsMouse ? Theme.textWhite : Theme.textMuted
                    }
                    MouseArea {
                        id: clearFilterMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: navPanel._applyFilter("")
                    }
                }
            }
        }

        // Tree view area
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Empty state
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 8
                visible: !navPanel.hasSource

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

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 8
                    visible: AppController.sampleFiles.length > 0
                    text: "— or open a sample —"
                    font.pixelSize: Theme.fontSizeXS
                    color: Theme.textMuted
                }

                Repeater {
                    model: AppController.sampleFiles

                    delegate: Label {
                        id: sampleLink
                        required property var modelData
                        Layout.alignment: Qt.AlignHCenter
                        text: sampleLink.modelData.title
                        font.pixelSize: Theme.fontSizeS
                        font.underline: sampleLinkMa.containsMouse
                        color: sampleLinkMa.containsMouse ? Theme.accent : Theme.textSecondary

                        MouseArea {
                            id: sampleLinkMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: AppController.openFile(sampleLink.modelData.url)
                        }
                    }
                }
            }

            // Zero-match state while a filter is active
            Label {
                anchors.centerIn: parent
                visible: navPanel.hasSource && treeView.rows === 0
                text: "No matches"
                font.pixelSize: Theme.fontSizeM
                color: Theme.textMuted
            }

            TreeView {
                id: treeView
                anchors.fill: parent
                model: null   // the current tab's tree, set when the tab changes
                visible: navPanel.hasSource
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
            visible: navPanel.hasSource

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
                    + "• Click the \u25B8 chevron to expand/collapse\n"
                    + "• Type in the filter box to narrow the tree"
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
                Label { text: "Ctrl+F";              font.pixelSize: Theme.fontSizeXS; font.family: Theme.fontMono; color: Theme.accentGold }
                Label { text: "Filter tree";         font.pixelSize: Theme.fontSizeXS; color: Theme.textSecondary }
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
                text: "Signal Map (DBC/LDF)"
                font.pixelSize: Theme.fontSizeM
                font.bold: true
                color: Theme.accent
            }

            GridLayout {
                columns: 2
                columnSpacing: 16
                rowSpacing: 4
                Layout.fillWidth: true

                Label { text: "\u2190 \u2191";            font.pixelSize: Theme.fontSizeXS; font.family: Theme.fontMono; color: Theme.accentGold }
                Label { text: "Previous signal";   font.pixelSize: Theme.fontSizeXS; color: Theme.textSecondary }
                Label { text: "\u2192 \u2193";            font.pixelSize: Theme.fontSizeXS; font.family: Theme.fontMono; color: Theme.accentGold }
                Label { text: "Next signal";       font.pixelSize: Theme.fontSizeXS; color: Theme.textSecondary }
                Label { text: "PgUp / PgDn";       font.pixelSize: Theme.fontSizeXS; font.family: Theme.fontMono; color: Theme.accentGold }
                Label { text: "Prev / next message"; font.pixelSize: Theme.fontSizeXS; color: Theme.textSecondary }
                Label { text: "Home / End";         font.pixelSize: Theme.fontSizeXS; font.family: Theme.fontMono; color: Theme.accentGold }
                Label { text: "First / last message"; font.pixelSize: Theme.fontSizeXS; color: Theme.textSecondary }
                Label { text: "Enter / Space";      font.pixelSize: Theme.fontSizeXS; font.family: Theme.fontMono; color: Theme.accentGold }
                Label { text: "Select signal in tree"; font.pixelSize: Theme.fontSizeXS; color: Theme.textSecondary }
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
