import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// App-level popup listing one tab's load diagnostics, opened from the tab badge.
Popup {
    id: diagnosticsPopup

    property string tabTitle: ""
    property var diagnostics: []

    function openFor(title, diags) {
        tabTitle = title
        diagnostics = diags
        open()
    }

    anchors.centerIn: Overlay.overlay
    width: Math.min((parent ? parent.width : 640) - 64, 560)
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
            Layout.fillWidth: true
            text: "Diagnostics — " + diagnosticsPopup.tabTitle
            font.pixelSize: Theme.fontSizeL
            font.bold: true
            color: Theme.textWhite
            elide: Text.ElideRight
        }

        Label {
            text: diagnosticsPopup.diagnostics.length
                  + (diagnosticsPopup.diagnostics.length === 1 ? " issue" : " issues")
            font.pixelSize: Theme.fontSizeXS
            color: Theme.textSecondary
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.border
        }

        ListView {
            id: diagList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 400)
            clip: true
            spacing: Theme.sp8
            rightMargin: Theme.sp8
            model: diagnosticsPopup.diagnostics

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            delegate: Rectangle {
                width: ListView.view.width - Theme.sp8
                implicitHeight: rowColumn.implicitHeight + Theme.sp12 * 2
                radius: Theme.radius
                color: Theme.bgCard
                border.color: Theme.border
                border.width: 1

                ColumnLayout {
                    id: rowColumn
                    anchors.fill: parent
                    anchors.margins: Theme.sp12
                    spacing: Theme.sp4

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.sp8

                        Label {
                            text: modelData.severity
                            font.pixelSize: Theme.fontSizeXS
                            font.bold: true
                            color: modelData.severity === "Error" ? Theme.accentRed : Theme.accentGold
                        }

                        Label {
                            Layout.fillWidth: true
                            text: modelData.title
                            font.pixelSize: Theme.fontSizeM
                            font.bold: true
                            color: Theme.textBright
                            wrapMode: Text.Wrap
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: modelData.detail.length > 0
                        text: modelData.detail
                        font.pixelSize: Theme.fontSizeXS
                        color: Theme.textSecondary
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        Button {
            text: "Close"
            Layout.alignment: Qt.AlignRight
            onClicked: diagnosticsPopup.close()

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
