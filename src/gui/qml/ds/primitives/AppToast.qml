import QtQuick
import QtQuick.Layouts
import PodStack.Design

// Aviso transitório do topo. Fila, dedup e duração vêm de `Feedback` (C++).
Rectangle {
    id: toast

    property string message: ""
    /// info | success | warning | error
    property string severity: "info"

    readonly property color tone: {
        if (severity === "success") {
            return Theme.success
        }
        if (severity === "warning") {
            return Theme.warning
        }
        if (severity === "error") {
            return Theme.danger
        }
        return Theme.indigo400
    }

    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    anchors.top: parent ? parent.top : undefined
    anchors.topMargin: Theme.topbarHeight + 8

    width: Math.min(560, toastRow.implicitWidth + 24)
    height: 38
    radius: Theme.radiusSm
    color: Theme.bgElevated
    border.width: 1
    border.color: Qt.rgba(toast.tone.r, toast.tone.g, toast.tone.b, 0.45)
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
            name: toast.severity === "success" ? "check"
                  : toast.severity === "info" ? "activity" : "alert"
            size: 14
            color: toast.tone
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
