import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Aba "Visão geral" do detalhe do container: métricas ao vivo.
ScrollView {
    id: root

    property string stateText: ""
    property string statusText: ""
    property int metricColumns: 2

    clip: true
    contentWidth: availableWidth
    ScrollBar.vertical: ScrollBar {}

    function formatBytes(value) {
        const number = Number(value || 0)
        if (number <= 0) {
            return "0 B"
        }
        const units = ["B", "kB", "MB", "GB", "TB"]
        let scaled = number
        let index = 0
        while (scaled >= 1000 && index < units.length - 1) {
            scaled /= 1000
            ++index
        }
        return scaled.toFixed(index === 0 ? 0 : 1) + " " + units[index]
    }

    ColumnLayout {
        x: 20
        y: 12
        width: Math.max(0, root.availableWidth - 40)
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: qsTr("Métricas ao vivo")
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
            }

            Text {
                text: backend.containerDetail.detail.loading ? qsTr("carregando inspect…")
                                                             : qsTr("stats ~1 s")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.metricColumns
            columnSpacing: 12
            rowSpacing: 12

            DetailMetricCard {
                label: qsTr("CPU")
                value: backend.containerDetail.detail.stats.cpu_percent || "—"
                detailText: qsTr("histórico de 60 amostras")
                values: backend.containerDetail.detail.cpuHistory
                accent: Theme.cyan
                maxValue: 100
            }

            DetailMetricCard {
                label: qsTr("RAM")
                value: {
                    const stats = backend.containerDetail.detail.stats
                    if (Number(stats.memLimitBytes || 0) > 0) {
                        return root.formatBytes(stats.memUsedBytes) + " / "
                               + root.formatBytes(stats.memLimitBytes)
                    }
                    return stats.mem_usage || "—"
                }
                detailText: backend.containerDetail.detail.stats.mem_percent || qsTr("aguardando amostras")
                values: backend.containerDetail.detail.memHistory
                accent: Theme.indigo400
                maxValue: 100
            }

            DetailMetricCard {
                label: qsTr("REDE  ↓ / ↑")
                value: {
                    const stats = backend.containerDetail.detail.stats
                    if (stats.netRxBytes !== undefined) {
                        return "↓ " + root.formatBytes(stats.netRxBytes) + "   ↑ "
                               + root.formatBytes(stats.netTxBytes)
                    }
                    return stats.net_io || "—"
                }
                detailText: qsTr("RX (ciano) e TX (índigo), acumulados")
                values: backend.containerDetail.detail.netRxHistory
                secondaryValues: backend.containerDetail.detail.netTxHistory
                accent: Theme.cyan
                autoScale: true
            }

            DetailMetricCard {
                label: qsTr("DISCO  R / W")
                value: {
                    const stats = backend.containerDetail.detail.stats
                    if (stats.diskReadBytes !== undefined) {
                        return "R " + root.formatBytes(stats.diskReadBytes) + "   W "
                               + root.formatBytes(stats.diskWriteBytes)
                    }
                    return stats.block_io || "—"
                }
                detailText: qsTr("leitura (ciano) e escrita (índigo), acumuladas")
                values: backend.containerDetail.detail.diskReadHistory
                secondaryValues: backend.containerDetail.detail.diskWriteHistory
                accent: Theme.cyan
                autoScale: true
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: qsTr("PIDs: %1 · estado: %2 · %3")
                          .arg(String(backend.containerDetail.detail.stats.pids || "—"))
                          .arg(root.stateText.length > 0 ? root.stateText : "—")
                          .arg(root.statusText)
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                elide: Text.ElideRight
            }

            Text {
                visible: backend.containerDetail.detail.statsError.length > 0
                text: backend.containerDetail.detail.statsError
                color: Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                elide: Text.ElideRight
            }
        }

        Item { Layout.preferredHeight: 12 }
    }
}
