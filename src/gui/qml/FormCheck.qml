import QtQuick
import QtQuick.Controls

CheckBox {
    id: control

    spacing: 8
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontMeta

    indicator: Rectangle {
        implicitWidth: 16
        implicitHeight: 16
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: 4
        color: control.checked ? Theme.indigo500 : Theme.bgSurface2
        border.width: 1
        border.color: control.checked ? Theme.indigo500
                                      : (control.hovered ? Theme.borderStrong : Theme.border)

        AppIcon {
            anchors.centerIn: parent
            name: "check"
            size: 11
            color: "#ffffff"
            visible: control.checked
        }
    }

    contentItem: Text {
        text: control.text
        color: control.enabled ? Theme.textPrimary : Theme.textDisabled
        font: control.font
        leftPadding: control.indicator.width + control.spacing
        verticalAlignment: Text.AlignVCenter
    }
}
