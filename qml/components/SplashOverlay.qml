import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: splash

    property bool loading: true
    property string statusText: "Loading\u2026"
    property real showTime: 0

    signal dismissed()

    color: Theme.bg
    z: 100

    function dismiss() {
        let elapsed = Date.now() - showTime
        let remaining = Math.max(0, 600 - elapsed)
        dismissTimer.interval = remaining
        dismissTimer.start()
    }

    Timer {
        id: dismissTimer
        onTriggered: fadeOut.start()
    }

    NumberAnimation {
        id: fadeOut
        target: splash
        property: "opacity"
        from: 1; to: 0
        duration: 350
        easing.type: Easing.InOutQuad
        onFinished: { splash.visible = false; splash.dismissed() }
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.sp16

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            source: "qrc:/resources/icons/explorer_128.png"
            width: 128; height: 128
            fillMode: Image.PreserveAspectFit
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Automotive Format Explorer"
            font.pixelSize: 22
            font.bold: true
            color: Theme.textPrimary
        }

        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            running: splash.visible
            palette.dark: Theme.accent
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: splash.statusText
            font.pixelSize: Theme.fontSizeXS
            color: Theme.textMuted
        }
    }
}
