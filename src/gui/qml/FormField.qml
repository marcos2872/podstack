import QtQuick
import QtQuick.Controls

// Campo de texto do redesign: 33 px, foco com acento.
TextField {
    id: control

    implicitHeight: 33
    leftPadding: 10
    rightPadding: 10
    color: Theme.textPrimary
    placeholderTextColor: Theme.textTertiary
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody
    selectByMouse: true
    selectionColor: Theme.accentSoft
    selectedTextColor: Theme.textPrimary

    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.bgSurface2
        border.width: 1
        border.color: control.activeFocus ? Theme.indigo500 : Theme.border

        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            color: Theme.accentSoft
            visible: control.activeFocus
            z: -1
        }
    }
}
