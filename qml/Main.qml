import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "components"
import ExplorerApp

ApplicationWindow {
    id: root
    width: 1360
    height: 860
    visible: false
    color: Theme.bg
    palette.accent: Theme.accent
    palette.highlight: Theme.accent
    palette.highlightedText: "white"
    title: "Automotive Format Explorer"

    property bool leftPaneVisible: true
    property real _savedLeftWidth: 320
    property bool _ready: false

    // Deferred startup — C++ handles show via DWM cloak.
    // Timer lets the first frame render before dismissing splash.
    Timer {
        interval: 100
        running: true
        onTriggered: {
            splashOverlay.showTime = Date.now()
            splashOverlay.dismiss()
        }
    }

    Component.onCompleted: _ready = true

    onLeftPaneVisibleChanged: {
        if (!_ready) return
        if (leftPaneVisible) {
            navPanel.SplitView.minimumWidth = 180
            navPanel.SplitView.preferredWidth = _savedLeftWidth
        } else {
            _savedLeftWidth = navPanel.SplitView.preferredWidth
            navPanel.SplitView.minimumWidth = 0
            navPanel.SplitView.preferredWidth = 0
        }
    }

    // --- Dialogs ---

    FileDialog {
        id: fileDialog
        title: "Open automotive file"
        fileMode: FileDialog.OpenFile
        nameFilters: [
            "Automotive files (*.dbc *.a2l *.ldf)",
            "DBC files (*.dbc)",
            "A2L files (*.a2l)",
            "LDF files (*.ldf)",
            "All files (*)"
        ]
        onAccepted: AppController.openFile(selectedFile)
    }

    Shortcut {
        sequence: "Ctrl+O"
        onActivated: fileDialog.open()
    }

    // Close current tab
    Shortcut {
        sequence: "Ctrl+W"
        enabled: AppController.currentTabIndex >= 0
        onActivated: AppController.closeTab(AppController.currentTabIndex)
    }

    // Next tab
    Shortcut {
        sequence: "Ctrl+Tab"
        enabled: tabs.count > 1
        onActivated: {
            let next = (AppController.currentTabIndex + 1) % tabs.count
            AppController.currentTabIndex = next
        }
    }

    // Previous tab
    Shortcut {
        sequence: "Ctrl+Shift+Tab"
        enabled: tabs.count > 1
        onActivated: {
            let prev = (AppController.currentTabIndex - 1 + tabs.count) % tabs.count
            AppController.currentTabIndex = prev
        }
    }

    // Toggle sidebar
    Shortcut {
        sequence: "Ctrl+Alt+B"
        onActivated: root.leftPaneVisible = !root.leftPaneVisible
    }

    // --- Main layout (starts hidden, fades in after splash) ---

    SplitView {
        id: mainLayout
        anchors.fill: parent
        orientation: Qt.Horizontal
        opacity: 0

        handle: Rectangle {
            implicitWidth: 6
            implicitHeight: 6
            color: "transparent"

            Rectangle {
                anchors.centerIn: parent
                width: 2
                height: parent.height
                color: SplitHandle.pressed ? Theme.accent
                     : SplitHandle.hovered ? Theme.borderHover
                     : Theme.border
            }
        }

        // --- Left pane: Nav panel ---
        NavPanel {
            id: navPanel
            SplitView.preferredWidth: 320
            SplitView.minimumWidth: root.leftPaneVisible ? 180 : 0
            clip: true
            treeModel: AppController.currentTreeModel
            Behavior on SplitView.preferredWidth {
                NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
            }
            onNodeSelected: function(nodeKey) {
                AppController.selectCurrentNode(nodeKey)
                if (centerPanelLoader.item && centerPanelLoader.item.scrollToNodeKey)
                    centerPanelLoader.item.scrollToNodeKey(nodeKey)
            }
            onCollapseRequested: root.leftPaneVisible = false
            onOpenRequested: fileDialog.open()
        }

        // --- Center/right: tabs + detail ---
        Rectangle {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 300
            color: Theme.bg

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // --- Header bar ---
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.headerHeight
                    color: Theme.bgHeader

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8

                        // Error label
                        Label {
                            Layout.fillWidth: true
                            color: Theme.accentRed
                            elide: Text.ElideRight
                            font.pixelSize: Theme.fontSizeM
                            text: AppController.lastError
                            visible: text.length > 0
                        }

                        // Spacer
                        Item { Layout.fillWidth: true; visible: AppController.lastError.length === 0 }

                        // Toggle left pane
                        Rectangle {
                            width: 24; height: 24
                            radius: Theme.radius
                            color: toggleLeftMa.containsMouse ? Theme.bgButtonHov : "transparent"
                            visible: !root.leftPaneVisible

                            Label {
                                anchors.centerIn: parent
                                text: "\u25B6"
                                font.pixelSize: 10
                                color: Theme.textSecondary
                            }
                            ToolTip.text: "Show sidebar"
                            ToolTip.visible: toggleLeftMa.containsMouse
                            ToolTip.delay: 400
                            MouseArea {
                                id: toggleLeftMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.leftPaneVisible = true
                            }
                        }
                    }
                }

                // --- Tab bar ---
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: tabs.count > 0 ? tabs.implicitHeight : 0
                    color: Theme.bgPanel
                    visible: tabs.count > 0

                    TabBar {
                        id: tabs
                        width: parent.width
                        currentIndex: AppController.currentTabIndex

                        background: Rectangle { color: "transparent" }

                        Repeater {
                            model: AppController.tabModel
                            delegate: TabButton {
                                id: tabDelegate
                                required property int index
                                required property string title
                                required property bool hasWarnings

                                width: Math.min(tabRow.implicitWidth + 24, Theme.tabWidthMax)
                                font.pixelSize: Theme.fontSizeM

                                palette.buttonText: tabs.currentIndex === index
                                    ? Theme.textWhite : Theme.textSecondary

                                contentItem: Row {
                                    id: tabRow
                                    spacing: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    leftPadding: 4

                                    Label {
                                        text: tabDelegate.hasWarnings ? tabDelegate.title + " \u26A0" : tabDelegate.title
                                        font.pixelSize: Theme.fontSizeM
                                        color: tabs.currentIndex === tabDelegate.index
                                            ? Theme.textWhite : Theme.textSecondary
                                        elide: Text.ElideRight
                                        anchors.verticalCenter: parent.verticalCenter
                                    }

                                    Rectangle {
                                        width: 16; height: 16
                                        radius: 3
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: tabCloseMa.containsMouse ? Theme.bgButtonHov : "transparent"

                                        Label {
                                            anchors.centerIn: parent
                                            text: "\u2715"
                                            font.pixelSize: 9
                                            color: tabCloseMa.containsMouse ? Theme.textWhite : Theme.textMuted
                                        }
                                        MouseArea {
                                            id: tabCloseMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: AppController.closeTab(tabDelegate.index)
                                        }
                                    }
                                }

                                background: Rectangle {
                                    color: tabs.currentIndex === tabDelegate.index
                                        ? Theme.bgActive : "transparent"
                                    radius: Theme.radius

                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width
                                        height: 2
                                        color: Theme.accent
                                        visible: tabs.currentIndex === tabDelegate.index
                                    }
                                }

                                onClicked: AppController.currentTabIndex = index
                            }
                        }
                    }
                }

                // --- Content area: center panel + detail ---
                SplitView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    orientation: Qt.Horizontal

                    handle: Rectangle {
                        implicitWidth: 6
                        implicitHeight: 6
                        color: "transparent"

                        Rectangle {
                            anchors.centerIn: parent
                            width: 2
                            height: parent.height
                            color: SplitHandle.pressed ? Theme.accent
                                 : SplitHandle.hovered ? Theme.borderHover
                                 : Theme.border
                        }
                    }

                    // --- Center panel (memory view or empty) ---
                    Loader {
                        id: centerPanelLoader
                        SplitView.preferredWidth: hasCenterPanel ? 520 : 0
                        SplitView.minimumWidth: hasCenterPanel ? 300 : 0
                        SplitView.maximumWidth: hasCenterPanel ? 99999 : 0
                        visible: hasCenterPanel
                        clip: true

                        property bool hasCenterPanel: AppController.centerPanelSource.toString() !== ""

                        function reload() {
                            let src = AppController.centerPanelSource
                            let model = AppController.centerPanelModel
                            if (src.toString() !== "" && model) {
                                centerPanelLoader.setSource(src, { "mapModel": model })
                            } else {
                                centerPanelLoader.source = ""
                            }
                        }

                        onLoaded: {
                            if (item) {
                                item.nodeKeyClicked.connect(function(nodeKey) {
                                    AppController.selectCurrentNode(nodeKey)
                                    navPanel.selectAndScrollTo(nodeKey)
                                })
                            }
                        }

                        Connections {
                            target: AppController
                            function onCurrentSessionChanged() {
                                centerPanelLoader.reload()
                            }
                        }
                    }

                    // --- Detail panel (right) ---
                    Rectangle {
                        id: detailPanel
                        SplitView.fillWidth: true
                        SplitView.minimumWidth: 250
                        color: Theme.bg

                        property bool showRawJson: false

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0

                            // Detail header with toggle
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.headerHeight
                                color: Theme.bgHeader
                                visible: detailList.count > 0

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 6
                                    spacing: 4

                                    Label {
                                        text: "DETAILS"
                                        font.pixelSize: Theme.fontSizeXS
                                        font.bold: true
                                        font.letterSpacing: 1.2
                                        color: Theme.textSecondary
                                        Layout.fillWidth: true
                                    }

                                    // Raw JSON toggle
                                    Rectangle {
                                        width: rawToggleRow.implicitWidth + 12
                                        height: 22
                                        radius: Theme.radius
                                        color: rawToggleMa.containsMouse
                                               ? Theme.bgButtonHov
                                               : (detailPanel.showRawJson ? Theme.bgActive : Theme.bgButton)
                                        visible: AppController.currentDetailModel
                                                 && AppController.currentDetailModel.rawJsonText.length > 0
                                        ToolTip.text: detailPanel.showRawJson ? "Show formatted view" : "Show raw proto JSON"
                                        ToolTip.visible: rawToggleMa.containsMouse
                                        ToolTip.delay: 300

                                        Row {
                                            id: rawToggleRow
                                            anchors.centerIn: parent
                                            spacing: 4

                                            Label {
                                                text: "{ }"
                                                font.family: Theme.fontMono
                                                font.pixelSize: Theme.fontSizeS
                                                font.bold: true
                                                color: detailPanel.showRawJson ? Theme.textWhite : Theme.textSecondary
                                            }
                                            Label {
                                                text: detailPanel.showRawJson ? "Cards" : "Proto"
                                                font.pixelSize: Theme.fontSizeXS
                                                color: detailPanel.showRawJson ? Theme.textWhite : Theme.textSecondary
                                            }
                                        }
                                        MouseArea {
                                            id: rawToggleMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: detailPanel.showRawJson = !detailPanel.showRawJson
                                        }
                                    }
                                }
                            }

                            // Thin accent strip
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 2
                                color: Theme.accent
                                opacity: 0.3
                                visible: detailList.count > 0
                            }

                            // Card view
                            ScrollView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.margins: Theme.sp8
                                visible: !detailPanel.showRawJson

                                ScrollBar.vertical.policy: ScrollBar.AsNeeded
                                ScrollBar.vertical.background: Rectangle { color: "transparent" }
                                ScrollBar.vertical.contentItem: Rectangle {
                                    implicitWidth: 6
                                    radius: 3
                                    color: parent.pressed ? Theme.bgButtonPrs
                                         : parent.hovered ? Theme.bgButtonHov : Theme.borderHover
                                }

                                ListView {
                                    id: detailList
                                    spacing: Theme.sp12
                                    clip: true
                                    rightMargin: Theme.sp12
                                    model: AppController.currentDetailModel

                                    delegate: Rectangle {
                                        width: ListView.view.width - Theme.sp12
                                        implicitHeight: cardLayout.implicitHeight + Theme.sp16 * 2
                                        radius: Theme.radius
                                        color: Theme.bgCard
                                        border.color: Theme.border
                                        border.width: 1

                                        ColumnLayout {
                                            id: cardLayout
                                            anchors.fill: parent
                                            anchors.margins: Theme.sp16
                                            spacing: Theme.sp8

                                            Label {
                                                Layout.fillWidth: true
                                                font.bold: true
                                                font.pixelSize: Theme.fontSizeL
                                                color: Theme.textBright
                                                text: model.title
                                            }

                                            Rectangle {
                                                Layout.fillWidth: true
                                                height: 1
                                                color: Theme.border
                                            }

                                            Repeater {
                                                model: fields

                                                delegate: RowLayout {
                                                    Layout.fillWidth: true
                                                    spacing: Theme.sp12

                                                    Label {
                                                        Layout.preferredWidth: 180
                                                        font.bold: true
                                                        font.pixelSize: Theme.fontSizeM
                                                        color: Theme.textSecondary
                                                        text: modelData.key + ":"
                                                    }

                                                    Label {
                                                        Layout.fillWidth: true
                                                        wrapMode: Text.WrapAnywhere
                                                        font.pixelSize: Theme.fontSizeM
                                                        color: Theme.textPrimary
                                                        text: modelData.value
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    // Empty state
                                    Label {
                                        anchors.centerIn: parent
                                        font.pixelSize: Theme.fontSizeL
                                        color: Theme.textMuted
                                        text: tabs.count === 0
                                              ? "Open a file to inspect its structure."
                                              : "Select a tree node to view details."
                                        visible: detailList.count === 0
                                    }
                                }
                            }

                            // Raw JSON view
                            ScrollView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.margins: Theme.sp8
                                visible: detailPanel.showRawJson

                                ScrollBar.vertical.policy: ScrollBar.AsNeeded
                                ScrollBar.vertical.background: Rectangle { color: "transparent" }
                                ScrollBar.vertical.contentItem: Rectangle {
                                    implicitWidth: 6
                                    radius: 3
                                    color: parent.pressed ? Theme.bgButtonPrs
                                         : parent.hovered ? Theme.bgButtonHov : Theme.borderHover
                                }

                                TextArea {
                                    id: rawJsonArea
                                    readOnly: true
                                    selectByMouse: true
                                    wrapMode: TextArea.WrapAnywhere
                                    font.family: Theme.fontMono
                                    font.pixelSize: Theme.fontSizeS
                                    color: Theme.textPrimary
                                    selectionColor: Theme.bgSelection
                                    selectedTextColor: Theme.textWhite
                                    text: AppController.currentDetailModel
                                          ? AppController.currentDetailModel.rawJsonText
                                          : ""

                                    background: Rectangle {
                                        color: Theme.bgCard
                                        radius: Theme.radius
                                        border.color: Theme.border
                                        border.width: 1
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // --- Edge toggle for collapsed left pane ---
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 16
        visible: !root.leftPaneVisible
        color: edgeToggleMa.containsMouse ? Theme.bgCardHov : "transparent"
        z: 2

        Label {
            anchors.centerIn: parent
            text: "\u25B6"
            font.pixelSize: 8
            color: Theme.textMuted
        }

        MouseArea {
            id: edgeToggleMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.leftPaneVisible = true
        }
    }

    // --- Toast notification ---
    Toast {
        id: toast
    }

    // Wire toast to controller events
    Connections {
        target: AppController
        function onLastErrorChanged() {
            if (AppController.lastError.length > 0)
                toast.show(AppController.lastError)
        }
        function onFileLoaded(displayName) {
            toast.show("Loaded " + displayName)
        }
    }

    // --- File loading overlay ---
    Rectangle {
        anchors.fill: parent
        color: "#80000000"
        visible: AppController.fileLoading
        z: 10

        Column {
            anchors.centerIn: parent
            spacing: 12

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: AppController.fileLoading
                palette.dark: Theme.accent
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Loading file\u2026"
                font.pixelSize: Theme.fontSizeL
                color: Theme.textWhite
            }
        }

        MouseArea {
            anchors.fill: parent
        }
    }

    // --- Splash overlay (on top of everything) ---
    SplashOverlay {
        id: splashOverlay
        anchors.fill: parent
        loading: AppController.startupLoading
        statusText: AppController.startupStatusText
        onDismissed: {
            mainFadeIn.start()
            AppController.startupLoading = false
        }
    }

    NumberAnimation {
        id: mainFadeIn
        target: mainLayout
        property: "opacity"
        from: 0; to: 1
        duration: 300
        easing.type: Easing.InOutQuad
    }
}
