import QtQuick
import QtQuick.Layouts
import PodStack.Design

// Card de métrica das estatísticas da machine.
Rectangle {
    id: card

    property string label: ""
    property string value: ""
    property string detail: ""

    Layout.fillWidth: true
    implicitHeight: 76
    radius: Theme.radius
    color: Theme.bgSurface2
    border.width: 1
    border.color: Theme.border

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 1

        Text {
            text: card.label
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.5
        }

        Text {
            text: card.value
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontHeading - 3
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        Text {
            text: card.detail
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }
}
