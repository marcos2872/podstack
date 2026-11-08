import QtQuick
import QtQuick.Controls

Button {
    id: control

    property color accentColor: "#6366F1"

    implicitHeight: 28
    font.pixelSize: 11

    background: Rectangle {
        radius: 6
        color: control.down ? Qt.darker(control.accentColor, 1.25) : control.accentColor
        opacity: control.enabled ? 1.0 : 0.35
    }

    contentItem: Text {
        text: control.text
        color: "#FFFFFF"
        font.pixelSize: 11
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
