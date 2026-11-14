import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Aba "Stats" do inspetor de machine: métricas coletadas via SSH.
Flickable {
    id: root

    property string machineState: ""

    readonly property var stats: backend.machineDetail.stats || ({})
    readonly property bool hasMachineStats: Object.keys(stats).length > 0
    readonly property string statsUpdatedAt: {
        const timestamp = Number(stats.ts || 0)
        return timestamp > 0 ? Qt.formatDateTime(new Date(timestamp), "HH:mm:ss") : ""
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

    clip: true
    contentWidth: width
    contentHeight: statsColumn.implicitHeight + 24
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    ColumnLayout {
        id: statsColumn

        x: 14
        y: 12
        width: root.width - 28
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                text: qsTr("Métricas via SSH")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                font.weight: Font.DemiBold
            }

            Item { Layout.fillWidth: true }

            Text {
                visible: root.hasMachineStats && root.statsUpdatedAt.length > 0
                text: qsTr("última amostra %1").arg(root.statsUpdatedAt)
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
            }
        }

        GridLayout {
            Layout.fillWidth: true
            visible: root.hasMachineStats
            columns: 2
            columnSpacing: 8
            rowSpacing: 8

            InspectorMetricCard {
                label: qsTr("CPU")
                value: root.formatPercent(root.stats.cpuPercent)
                detail: qsTr("uso da machine")
            }

            InspectorMetricCard {
                label: qsTr("RAM")
                value: root.formatPercent(root.stats.memPercent)
                detail: root.formatBytes(root.stats.memUsed) + " / "
                        + root.formatBytes(root.stats.memTotal)
            }

            InspectorMetricCard {
                label: qsTr("Disco")
                value: root.formatPercent(root.stats.diskPercent)
                detail: root.formatBytes(root.stats.diskUsed) + " / "
                        + root.formatBytes(root.stats.diskTotal)
            }

            InspectorMetricCard {
                label: qsTr("Rede")
                value: "↓ " + root.formatRate(root.stats.netRx)
                detail: "↑ " + root.formatRate(root.stats.netTx)
            }

            InspectorMetricCard {
                label: qsTr("I/O de disco")
                value: "L " + root.formatRate(root.stats.diskRead)
                detail: "E " + root.formatRate(root.stats.diskWrite)
            }
        }

        GridLayout {
            Layout.fillWidth: true
            visible: root.hasMachineStats
            columns: 2
            columnSpacing: 8
            rowSpacing: 8

            InspectorChartBox {
                label: qsTr("CPU (%)")
                values: backend.machineDetail.cpuHistory
                lineColor: Theme.cyan
            }

            InspectorChartBox {
                label: qsTr("RAM (%)")
                values: backend.machineDetail.memHistory
                lineColor: Theme.indigo400
            }

            InspectorChartBox {
                label: qsTr("Rede ↓")
                values: backend.machineDetail.netRxHistory
                lineColor: Theme.cyan
                autoScale: true
            }

            InspectorChartBox {
                label: qsTr("Rede ↑")
                values: backend.machineDetail.netTxHistory
                lineColor: Theme.indigo400
                autoScale: true
            }
        }

        Text {
            Layout.fillWidth: true
            visible: backend.machineDetail.statsError.length > 0
            text: backend.machineDetail.statsError
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            visible: !root.hasMachineStats && backend.machineDetail.statsError.length === 0
            text: root.machineState === "running"
                  ? qsTr("Aguardando amostras…")
                  : qsTr("Inicie a machine para coletar métricas.")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            visible: root.hasMachineStats && root.machineState !== "running"
            text: qsTr("A machine não está em execução; as métricas podem estar desatualizadas.")
            color: Theme.warning
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }
    }
}
