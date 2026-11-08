import QtQuick
import QtQuick.Controls
import PodStack

Rectangle {
    id: toast

    property string message
    property string kind

    Theme { id: theme }

    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    anchors.top: parent ? parent.top : undefined
    anchors.topMargin: 64

    width: Math.min(560, messageLabel.implicitWidth + 32)
    height: 38
    radius: 8
    color: kind === "die" || kind === "stop" ? theme.warning : theme.indigo700
    opacity: message.length > 0 ? 1 : 0
    visible: opacity > 0

    Behavior on opacity {
        NumberAnimation { duration: 180 }
    }

    Label {
        id: messageLabel
        anchors.centerIn: parent
        text: toast.message
        color: "#FFFFFF"
        font.pixelSize: 12
        elide: Text.ElideRight
        width: Math.min(implicitWidth, 520)
    }
}
