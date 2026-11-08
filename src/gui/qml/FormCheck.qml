import QtQuick
import QtQuick.Controls
import PodStack

CheckBox {
    id: control

    Theme { id: theme }

    contentItem: Text {
        text: control.text
        color: theme.textPrimary
        font.pixelSize: 11
        leftPadding: 24
        verticalAlignment: Text.AlignVCenter
    }
}
