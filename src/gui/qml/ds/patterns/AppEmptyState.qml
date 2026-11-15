import QtQuick
import QtQuick.Controls
import PodStack.Design

// Estado vazio padrão de listas e painéis.
Item {
    id: root

    property string iconName: "search"
    property string title: ""
    property string description: ""
    property string actionText: ""
    signal actionClicked()

    implicitHeight: column.implicitHeight

    Column {
        id: column
        anchors.centerIn: parent
        width: Math.min(parent.width, 360)
        spacing: Theme.spacingMd

        AppIcon {
            anchors.horizontalCenter: parent.horizontalCenter
            name: root.iconName
            size: 28
            color: Theme.textTertiary
        }

        Text {
            width: parent.width
            text: root.title
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        Text {
            width: parent.width
            visible: root.description.length > 0
            text: root.description
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        AppButton {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.actionText.length > 0
            text: root.actionText
            variant: "primary"
            onClicked: root.actionClicked()
        }
    }
}
