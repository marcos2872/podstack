import QtQuick
import QtQuick.Layouts

// Cabeçalho de página: título, subtítulo e ações à direita.
RowLayout {
    id: root

    property string title
    property string subtitle
    default property alias actions: actionsRow.data

    Layout.fillWidth: true
    spacing: 12

    ColumnLayout {
        spacing: 4

        Text {
            text: root.title
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontDisplay
            font.weight: Font.DemiBold
            font.letterSpacing: -0.7
        }

        Text {
            text: root.subtitle
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
        }
    }

    Item { Layout.fillWidth: true }

    RowLayout {
        id: actionsRow
        spacing: 7
    }
}
