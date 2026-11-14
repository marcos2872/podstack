import QtQuick
import QtQuick.Layouts
import PodStack.Design

// Item de resumo (rótulo + valor) da etapa final do assistente.
ColumnLayout {
    property string label: ""
    property string value: ""
    Layout.fillWidth: true
    spacing: 1

    Text {
        Layout.fillWidth: true
        text: parent.label
        color: Theme.textTertiary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontMicro
    }

    Text {
        Layout.fillWidth: true
        text: parent.value
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }
}
