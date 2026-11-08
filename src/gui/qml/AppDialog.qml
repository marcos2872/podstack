import QtQuick
import QtQuick.Controls
import PodStack

// Diálogo base do redesign: cantos arredondados, padding e header próprio.
Dialog {
    id: control

    modal: true
    anchors.centerIn: parent
    padding: 0
    leftPadding: 18
    rightPadding: 18
    topPadding: 0
    bottomPadding: 18

    background: Rectangle {
        color: Theme.bgSurface
        radius: Theme.radiusLg
        border.width: 1
        border.color: Theme.border
    }

    header: Item {
        implicitHeight: 56

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.top: parent.top
            anchors.topMargin: 14
            text: control.title
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: 17
            font.weight: Font.DemiBold
            font.letterSpacing: -0.3
            elide: Text.ElideRight
        }
    }
}
