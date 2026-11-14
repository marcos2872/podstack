import QtQuick
import QtQuick.Layouts
import PodStack.Design

// Banner de erro com ação opcional de repetir.
Rectangle {
    id: root

    property string message: ""
    property string actionText: qsTr("Tentar novamente")
    signal actionClicked()

    implicitHeight: 40
    radius: Theme.radiusSm
    color: Theme.dangerSoft
    border.width: 1
    border.color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.35)
    visible: message.length > 0

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingSm
        spacing: Theme.spacingSm

        AppIcon {
            name: "alert"
            size: 14
            color: Theme.danger
        }

        Text {
            Layout.fillWidth: true
            text: root.message
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            elide: Text.ElideRight
        }

        AppButton {
            visible: root.actionText.length > 0
            text: root.actionText
            variant: "ghost"
            size: "sm"
            onClicked: root.actionClicked()
        }
    }
}
