import QtQuick
import QtQuick.Layouts
import PodStack.Design

// Card de métrica com sparkline das abas do detalhe do container.
Rectangle {
    id: card

    property string label: ""
    property string value: ""
    property string detailText: ""
    property var values: []
    property var secondaryValues: []
    property color accent: Theme.cyan
    property real maxValue: 0
    property bool autoScale: false

    Layout.fillWidth: true
    implicitHeight: 142
    radius: Theme.radius
    color: Theme.bgSurface2
    border.width: 1
    border.color: Theme.border

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 2

        Text {
            text: card.label
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.8
        }

        Text {
            Layout.fillWidth: true
            text: card.value
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontHeading - 3
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            text: card.detailText
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            elide: Text.ElideRight
        }

        AppSparkline {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 24
            values: card.values
            lineColor: card.accent
            maxValue: card.maxValue
            autoScale: card.autoScale
        }

        AppSparkline {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            visible: card.secondaryValues.length > 0
            values: card.secondaryValues
            lineColor: Theme.indigo400
            maxValue: card.maxValue
            autoScale: card.autoScale
        }
    }
}
