import QtQuick
import QtQuick.Layouts
import PodStack.Design

// Caixa de sparkline das estatísticas da machine.
Rectangle {
    id: chart

    property string label: ""
    property var values: []
    property color lineColor: Theme.cyan
    property bool autoScale: false

    Layout.fillWidth: true
    Layout.preferredHeight: 78
    radius: Theme.radius
    color: Theme.bgSurface2
    border.width: 1
    border.color: Theme.border

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 2

        Text {
            text: chart.label
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
        }

        AppSparkline {
            Layout.fillWidth: true
            Layout.fillHeight: true
            values: chart.values
            maxValue: chart.autoScale ? 0 : 100
            autoScale: chart.autoScale
            lineColor: chart.lineColor
        }
    }
}
