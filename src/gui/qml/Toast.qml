import QtQuick
import QtQuick.Layouts
import PodStack

Rectangle {
    id: toast

    property string message
    property string kind

    readonly property bool warning: kind === "die" || kind === "stop" || kind === "remove"
    readonly property bool success: kind === "start" || kind === "create"

    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    anchors.top: parent ? parent.top : undefined
    anchors.topMargin: Theme.topbarHeight + 8

    width: Math.min(560, toastRow.implicitWidth + 24)
    height: 38
    radius: Theme.radiusSm
    color: Theme.bgElevated
    border.width: 1
    border.color: Theme.borderStrong
    opacity: message.length > 0 ? 1 : 0
    visible: opacity > 0

    Behavior on opacity {
        NumberAnimation { duration: Theme.duration }
    }

    RowLayout {
        id: toastRow
        anchors.centerIn: parent
        spacing: 8

        AppIcon {
            name: toast.warning ? "alert" : toast.success ? "check" : "activity"
            size: 14
            color: toast.warning ? Theme.warning
                                 : toast.success ? Theme.success : Theme.indigo400
            Layout.alignment: Qt.AlignVCenter
        }

        Text {
            text: toast.message
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            Layout.preferredWidth: Math.min(implicitWidth, 500)
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
