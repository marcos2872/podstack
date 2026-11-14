import QtQuick
import QtQuick.Layouts
import PodStack.Design

// Seção com título e conteúdo.
ColumnLayout {
    id: root

    property string title: ""
    property string description: ""
    default property alias content: body.data
    spacing: Theme.spacingSm

    Text {
        Layout.fillWidth: true
        visible: root.title.length > 0
        text: root.title
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
    }

    Text {
        Layout.fillWidth: true
        visible: root.description.length > 0
        text: root.description
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        wrapMode: Text.Wrap
    }

    ColumnLayout {
        id: body
        Layout.fillWidth: true
        spacing: Theme.spacingSm
    }
}
