import QtQuick
import QtQuick.Controls
import PodStack.Design

// ToolTip padronizado.
ToolTip {
    id: control

    delay: 400
    timeout: 4000
    padding: 8

    contentItem: Text {
        text: control.text
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontMeta
    }

    background: Rectangle {
        color: Theme.bgElevated
        radius: Theme.radiusXs
        border.width: 1
        border.color: Theme.border
    }
}
