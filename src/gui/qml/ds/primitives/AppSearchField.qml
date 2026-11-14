import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Campo de busca compacto (ícone + texto), usado no topbar e em listas.
Rectangle {
    id: root

    property alias text: field.text
    property string placeholderText: qsTr("Buscar…")
    property string accessibleName: qsTr("Buscar")

    implicitHeight: 34
    radius: Theme.radiusSm
    color: Theme.bgSurface2
    border.width: 1
    border.color: field.activeFocus ? Theme.indigo500 : Theme.border

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 7

        AppIcon {
            name: "search"
            size: 14
            color: Theme.textTertiary
        }

        TextField {
            id: field
            Layout.fillWidth: true
            Accessible.name: root.accessibleName
            placeholderText: root.placeholderText
            color: Theme.textPrimary
            placeholderTextColor: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            leftPadding: 0
            rightPadding: 0
            verticalAlignment: TextInput.AlignVCenter
            background: null
            selectByMouse: true
            selectionColor: Theme.accentSoft
            selectedTextColor: Theme.textPrimary
        }
    }
}
