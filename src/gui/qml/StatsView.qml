import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: statsView

    Theme { id: theme }

    spacing: 8

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 8
        rowSpacing: 8

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            color: theme.bgElevated
            radius: 6
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                Label {
                    text: "CPU"
                    color: theme.textSecondary
                    font.pixelSize: 10
                }
                Label {
                    text: backend.stats.cpu_percent || "—"
                    color: theme.cyan
                    font.pixelSize: 15
                    font.bold: true
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            color: theme.bgElevated
            radius: 6
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                Label {
                    text: "MEMÓRIA"
                    color: theme.textSecondary
                    font.pixelSize: 10
                }
                Label {
                    text: backend.stats.mem_usage || "—"
                    color: theme.textPrimary
                    font.pixelSize: 13
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            color: theme.bgElevated
            radius: 6
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                Label {
                    text: "REDE (RX / TX)"
                    color: theme.textSecondary
                    font.pixelSize: 10
                }
                Label {
                    text: backend.stats.net_io || "—"
                    color: theme.textPrimary
                    font.pixelSize: 13
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            color: theme.bgElevated
            radius: 6
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                Label {
                    text: "DISCO / PIDS"
                    color: theme.textSecondary
                    font.pixelSize: 10
                }
                Label {
                    text: (backend.stats.block_io || "—") + "  ·  " + (backend.stats.pids || "—")
                    color: theme.textPrimary
                    font.pixelSize: 13
                }
            }
        }
    }

    Canvas {
        id: chart
        Layout.fillWidth: true
        Layout.preferredHeight: 120

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)

            const history = backend.cpuHistory
            if (history.length < 2) {
                return
            }

            ctx.strokeStyle = theme.cyan
            ctx.lineWidth = 2
            ctx.beginPath()
            for (let i = 0; i < history.length; ++i) {
                const x = (i / (history.length - 1)) * width
                const y = height - (Math.min(history[i], 100) / 100) * height
                if (i === 0) {
                    ctx.moveTo(x, y)
                } else {
                    ctx.lineTo(x, y)
                }
            }
            ctx.stroke()
        }

        Connections {
            target: backend
            function onStatsChanged() {
                chart.requestPaint()
            }
        }
    }

    Label {
        text: "CPU (%) — últimas amostras"
        color: theme.textSecondary
        font.pixelSize: 10
    }

    Item { Layout.fillHeight: true }
}
