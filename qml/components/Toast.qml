import QtQuick
import QtQuick.Controls

Rectangle {
    id: toast

    function show(message) {
        hideFinish.stop()
        toastLabel.text = message
        toast.opacity = 1.0
        toast.visible = true
        hideTimer.restart()
    }

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 40
    width: Math.min(toastLabel.implicitWidth + Theme.sp16 * 2, 500)
    height: toastLabel.implicitHeight + Theme.sp12 * 2
    radius: Theme.radius
    color: Theme.bgHeader
    border.color: Theme.borderHover
    border.width: 1
    visible: false
    opacity: 0
    z: 200

    Behavior on opacity {
        NumberAnimation { duration: 300; easing.type: Easing.InOutQuad }
    }

    Label {
        id: toastLabel
        anchors.centerIn: parent
        width: Math.min(implicitWidth, 460)
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: Theme.fontSizeM
        color: Theme.textPrimary
    }

    Timer {
        id: hideTimer
        interval: 2500
        onTriggered: {
            toast.opacity = 0
            hideFinish.start()
        }
    }

    Timer {
        id: hideFinish
        interval: 300
        onTriggered: toast.visible = false
    }
}
