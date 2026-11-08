import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

Flickable {
    id: statsView

    contentWidth: width
    contentHeight: statsColumn.implicitHeight + 28
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    readonly property var cpuValues: backend.cpuHistory.slice(Math.max(0, backend.cpuHistory.length - 30))
    readonly property var memValues: backend.memHistory.slice(Math.max(0, backend.memHistory.length - 30))

    ColumnLayout {
        id: statsColumn
        x: 14
        y: 14
        width: statsView.width - 28
        spacing: 10

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10

            // CPU
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 62
                color: Theme.bgSurface2
                radius: Theme.radiusSm
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 2

                    Text {
                        text: qsTr("CPU")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.8
                    }

                    Text {
                        text: backend.stats.cpu_percent || "—"
                        color: Theme.cyan
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTitle
                        font.weight: Font.DemiBold
                    }
                }
            }

            // Memória
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 62
                color: Theme.bgSurface2
                radius: Theme.radiusSm
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 2

                    Text {
                        text: qsTr("MEMÓRIA")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.8
                    }

                    Text {
                        Layout.fillWidth: true
                        text: backend.stats.mem_usage || "—"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Text {
                        visible: (backend.stats.mem_percent || "").length > 0
                        text: backend.stats.mem_percent
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }
                }
            }

            // Rede
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 62
                color: Theme.bgSurface2
                radius: Theme.radiusSm
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 2

                    Text {
                        text: qsTr("REDE (RX / TX)")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.8
                    }

                    Text {
                        Layout.fillWidth: true
                        text: backend.stats.net_io || "—"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                }
            }

            // Disco / PIDs
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 62
                color: Theme.bgSurface2
                radius: Theme.radiusSm
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 2

                    Text {
                        text: qsTr("DISCO / PIDS")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.8
                    }

                    Text {
                        Layout.fillWidth: true
                        text: (backend.stats.block_io || "—") + "  ·  "
                              + (backend.stats.pids !== undefined ? backend.stats.pids : "—")
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                }
            }
        }

        // CPU
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 92
            color: Theme.bgSurface2
            radius: Theme.radiusSm
            border.width: 1
            border.color: Theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 2

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: qsTr("CPU (%)")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.8
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: statsView.cpuValues.length > 0
                              ? statsView.cpuValues[statsView.cpuValues.length - 1].toFixed(1) + "%"
                              : ""
                        color: Theme.cyan
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }
                }

                Sparkline {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    values: statsView.cpuValues
                    lineColor: Theme.cyan
                    maxValue: 100
                }
            }
        }

        // RAM
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 92
            color: Theme.bgSurface2
            radius: Theme.radiusSm
            border.width: 1
            border.color: Theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 2

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: qsTr("RAM (%)")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.8
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: statsView.memValues.length > 0
                              ? statsView.memValues[statsView.memValues.length - 1].toFixed(1) + "%"
                              : ""
                        color: Theme.indigo400
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }
                }

                Sparkline {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    values: statsView.memValues
                    lineColor: Theme.indigo400
                    autoScale: true
                }
            }
        }

    }
}
