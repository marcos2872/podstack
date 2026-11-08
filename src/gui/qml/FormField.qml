import QtQuick
import QtQuick.Controls
import PodStack

TextField {
    id: control

    Theme { id: theme }

    color: theme.textPrimary
    placeholderTextColor: theme.textSecondary
    font.pixelSize: 12
    selectByMouse: true

    background: Rectangle {
        color: theme.bgElevated
        radius: 6
        border.color: theme.border
        border.width: 1
    }
}
