import QtQuick
import QtQuick.Layouts
import PodStack.Design

// Card de etapa do assistente de primeira execução.
Rectangle {
    id: card

    property string title: ""
    default property alias content: body.data
    implicitHeight: cardLayout.implicitHeight + 28
    radius: Theme.radiusLg
    color: Theme.bgSurface
    border.width: 1
    border.color: Theme.border

    ColumnLayout {
        id: cardLayout
        anchors.fill: parent
        anchors.margins: 14
        spacing: 8

        Text {
            Layout.fillWidth: true
            visible: card.title.length > 0
            text: card.title
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontUi
            font.weight: Font.DemiBold
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: 8
        }
    }
}
