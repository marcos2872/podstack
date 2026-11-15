import QtQuick
import QtQuick.Layouts
import PodStack.Design

// Card compacto de métrica (CPU, RAM, disco, rede).
Rectangle {
    id: root

    property string label: ""
    property string value: ""
    property string detail: ""
    property color accent: Theme.indigo400

    implicitHeight: 74
    radius: Theme.radius
    color: Theme.bgSurface2
    border.width: 1
    border.color: Theme.border

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: 2

        Text {
            text: root.label.toUpperCase()
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            font.letterSpacing: 0.8
        }

        Text {
            Layout.fillWidth: true
            text: root.value
            color: root.accent
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontHeading
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            visible: root.detail.length > 0
            text: root.detail
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMeta
            elide: Text.ElideRight
        }
    }
}
