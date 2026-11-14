import QtQuick
import QtQuick.Controls
import PodStack.Design

// Switch com as cores e dimensões do tema.
Switch {
    id: control

    implicitHeight: Theme.controlHeightSm
    spacing: Theme.spacingSm

    indicator: Rectangle {
        implicitWidth: 34
        implicitHeight: 20
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: height / 2
        color: control.checked ? Theme.indigo500 : Theme.bgSurface2
        border.width: 1
        border.color: control.checked ? Theme.indigo500 : Theme.borderStrong

        Rectangle {
            x: control.checked ? parent.width - width - 2 : 2
            y: 2
            width: 16
            height: 16
            radius: 8
            color: "#ffffff"
            Behavior on x {
                NumberAnimation { duration: Theme.duration }
            }
        }
    }

    contentItem: Text {
        text: control.text
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
    }
}
