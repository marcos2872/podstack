import QtQuick
import QtQuick.Layouts
import PodStack.Design

// Título (e subtítulo) de uma etapa do assistente.
ColumnLayout {
    property string title: ""
    property string subtitle: ""
    property bool centered: false
    Layout.fillWidth: true
    spacing: 2

    Text {
        Layout.fillWidth: true
        horizontalAlignment: parent.centered ? Text.AlignHCenter : Text.AlignLeft
        text: parent.title
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontDisplay
        font.weight: Font.DemiBold
        font.letterSpacing: -0.4
        wrapMode: Text.WordWrap
    }

    Text {
        Layout.fillWidth: true
        horizontalAlignment: parent.centered ? Text.AlignHCenter : Text.AlignLeft
        visible: parent.subtitle.length > 0
        text: parent.subtitle
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        wrapMode: Text.WordWrap
    }
}
