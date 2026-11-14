import QtQuick
import QtQuick.Layouts
import PodStack.Design

// Título de seção usado nas abas do detalhe do container.
Text {
    property string title: ""

    Layout.fillWidth: true
    Layout.topMargin: 4
    text: title
    color: Theme.textTertiary
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontMicro
    font.weight: Font.DemiBold
    font.capitalization: Font.AllUppercase
    font.letterSpacing: 0.8
}
