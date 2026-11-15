import QtQuick
import QtQuick.Layouts
import PodStack.Design

// Linha rótulo/valor das abas do detalhe do container.
RowLayout {
    id: root

    property string label: ""
    property string value: ""

    Layout.fillWidth: true
    spacing: 12

    Text {
        Layout.preferredWidth: 128
        text: root.label
        color: Theme.textTertiary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontMicro
        wrapMode: Text.WordWrap
    }

    Text {
        Layout.fillWidth: true
        text: root.value
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontMeta
        wrapMode: Text.WrapAnywhere
    }
}
