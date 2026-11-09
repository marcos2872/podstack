import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import PodStack

// Activity Monitor: CPU/memória/rede do host e consumo por container.
// Os dados chegam de `system.stats.subscribe` (uma amostra a cada 2 s).
Window {
    id: monitor

    width: 900
    height: 620
    minimumWidth: 720
    minimumHeight: 460
    visible: false
    title: qsTr("Activity Monitor — PodStack")
    color: Theme.bgBase

    readonly property var host: backend.activityStats.host || ({})
    readonly property var totals: backend.activityStats.totals || ({})
    readonly property var containers: backend.activityStats.containers || []

    function open() {
        backend.startActivityMonitor()
        show()
        raise()
        requestActivate()
    }

    function formatBytes(value) {
        const number = Number(value || 0)
        if (number < 1024) {
            return number.toFixed(0) + " B"
        }
        const units = ["KiB", "MiB", "GiB", "TiB"]
        let scaled = number / 1024
        let index = 0
        while (scaled >= 1024 && index < units.length - 1) {
            scaled /= 1024
            ++index
        }
        return scaled.toFixed(1) + " " + units[index]
    }

    function formatRate(value) {
        return formatBytes(value) + "/s"
    }

    function formatPercent(value) {
        return Number(value || 0).toFixed(1) + "%"
    }

    onClosing: function(close) {
        backend.stopActivityMonitor()
    }

    component MetricCard: Rectangle {
        id: card

        property string label: ""
        property string value: ""
        property string detail: ""

        Layout.fillWidth: true
        implicitHeight: 82
        radius: Theme.radius
        color: Theme.bgSurface
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
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.6
            }

            Text {
                text: card.value
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontHeading
                font.weight: Font.DemiBold
            }

            Text {
                text: card.detail
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                text: qsTr("Activity Monitor")
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontDisplay
                font.weight: Font.DemiBold
            }

            Text {
                text: backend.activityActive ? qsTr("ao vivo · 2 s") : qsTr("parado")
                color: backend.activityActive ? Theme.success : Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
            }

            Item { Layout.fillWidth: true }

            ActionButton {
                text: qsTr("Fechar")
                variant: "secondary"
                onClicked: monitor.close()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            MetricCard {
                label: qsTr("CPU do host")
                value: monitor.formatPercent(monitor.host.cpuPercent)
                detail: qsTr("todos os núcleos")
            }

            MetricCard {
                label: qsTr("Memória")
                value: monitor.formatPercent(monitor.host.memPercent)
                detail: monitor.formatBytes(monitor.host.memUsed) + " / "
                        + monitor.formatBytes(monitor.host.memTotal)
            }

            MetricCard {
                label: qsTr("Rede")
                value: "↓ " + monitor.formatRate(monitor.host.netRx)
                detail: "↑ " + monitor.formatRate(monitor.host.netTx)
            }

            MetricCard {
                label: qsTr("Containers")
                value: String(monitor.containers.length)
                detail: monitor.formatPercent(monitor.totals.cpuPercent) + " · "
                        + monitor.formatBytes(monitor.totals.memUsed) + " · "
                        + monitor.formatBytes(monitor.totals.netRx) + "/s"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 110
                radius: Theme.radius
                color: Theme.bgSurface
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 2

                    Text {
                        text: qsTr("CPU do host (%)")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }

                    Sparkline {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        values: backend.activityCpuHistory
                        maxValue: 100
                        lineColor: Theme.cyan
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 110
                radius: Theme.radius
                color: Theme.bgSurface
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 2

                    Text {
                        text: qsTr("Memória do host (%)")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }

                    Sparkline {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        values: backend.activityMemHistory
                        maxValue: 100
                        lineColor: Theme.indigo400
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radius
            color: Theme.bgSurface
            border.width: 1
            border.color: Theme.border
            clip: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 12
                    spacing: 12

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("CONTAINER")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.preferredWidth: 90
                        text: qsTr("CPU")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignRight
                    }
                    Text {
                        Layout.preferredWidth: 150
                        text: qsTr("MEMÓRIA")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignRight
                    }
                    Text {
                        Layout.preferredWidth: 170
                        text: qsTr("REDE (RX/TX)")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignRight
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.border
                }

                Text {
                    Layout.fillWidth: true
                    Layout.margins: 24
                    visible: monitor.containers.length === 0
                    text: qsTr("Nenhum container em execução.")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    horizontalAlignment: Text.AlignHCenter
                }

                ListView {
                    id: containerList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: monitor.containers
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Rectangle {
                        required property var modelData

                        width: containerList.width
                        height: 44
                        color: rowHover.hovered ? Theme.bgHover : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 12

                            Text {
                                Layout.fillWidth: true
                                text: modelData.name || modelData.id
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.preferredWidth: 90
                                text: monitor.formatPercent(modelData.cpuPercent)
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                horizontalAlignment: Text.AlignRight
                            }
                            Text {
                                Layout.preferredWidth: 150
                                text: monitor.formatBytes(modelData.memUsed) + " ("
                                      + monitor.formatPercent(modelData.memPercent) + ")"
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                horizontalAlignment: Text.AlignRight
                            }
                            Text {
                                Layout.preferredWidth: 170
                                text: monitor.formatRate(modelData.netRx) + " / "
                                      + monitor.formatRate(modelData.netTx)
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                horizontalAlignment: Text.AlignRight
                            }
                        }

                        HoverHandler { id: rowHover }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: Theme.border
                        }
                    }
                }
            }
        }
    }
}
