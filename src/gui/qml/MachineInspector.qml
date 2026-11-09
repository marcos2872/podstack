import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

// Detalhe de uma machine: Info, Logs, Stats e ações de ciclo de vida.
Rectangle {
    id: root

    property var machine: ({})
    property string tab: "info"

    signal deleteRequested(string id)

    readonly property string machineId: machine && machine.id ? String(machine.id) : ""
    readonly property string machineName: machine && machine.name ? String(machine.name)
                                                                 : machineId
    readonly property string machineState: machine && machine.state ? String(machine.state) : ""
    readonly property var portsList: machine && machine.ports ? machine.ports : []
    readonly property var mountsList: machine && machine.mounts ? machine.mounts : []
    readonly property var stats: backend.machineStats || ({})
    readonly property bool hasMachineStats: Object.keys(stats).length > 0
    readonly property string statsUpdatedAt: {
        const timestamp = Number(stats.ts || 0)
        return timestamp > 0 ? Qt.formatDateTime(new Date(timestamp), "HH:mm:ss") : ""
    }

    function formatGiB(bytes) {
        const value = Number(bytes || 0) / 1073741824
        return value > 0 ? value.toFixed(1) + " GiB" : "-"
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

    function selectTab(nextTab) {
        if (root.tab === "stats" && nextTab !== "stats") {
            backend.stopMachineStats()
        }
        root.tab = nextTab
        if (nextTab === "logs") {
            backend.loadMachineLogs(root.machineId, 200)
        } else if (nextTab === "stats" && root.machineId.length > 0) {
            backend.startMachineStats(root.machineId)
        }
    }

    onMachineIdChanged: {
        if (root.tab === "stats") {
            backend.stopMachineStats()
            if (root.machineId.length > 0) {
                backend.startMachineStats(root.machineId)
            }
        }
    }

    Component.onDestruction: {
        if (root.tab === "stats") {
            backend.stopMachineStats()
        }
    }

    function stateLabel(state) {
        if (state === "running") {
            return qsTr("em execução")
        }
        if (state === "starting") {
            return qsTr("iniciando")
        }
        if (state === "stopping") {
            return qsTr("parando")
        }
        if (state === "stopped") {
            return qsTr("parada")
        }
        if (state === "provisioning") {
            return qsTr("provisionando")
        }
        if (state === "deleting" || state === "deleted") {
            return qsTr("removendo")
        }
        if (state === "error") {
            return qsTr("erro")
        }
        return state.length > 0 ? state : qsTr("desconhecida")
    }

    function scrollLogsToEnd() {
        const flick = machineLogScroll.contentItem
        if (flick) {
            flick.contentY = Math.max(0, flick.contentHeight - flick.height)
        }
    }

    function stateTone(state) {
        if (state === "running") {
            return "success"
        }
        if (state === "error") {
            return "danger"
        }
        if (state === "starting" || state === "stopping" || state === "provisioning") {
            return "warning"
        }
        return "muted"
    }

    function portsText() {
        if (portsList.length === 0) {
            return "-"
        }
        const values = []
        for (let i = 0; i < portsList.length; ++i) {
            const port = portsList[i]
            const label = port.label ? String(port.label) + ": " : ""
            values.push(label + "127.0.0.1:" + port.hostPort + " → "
                        + port.guestPort + "/" + port.protocol
                        + (port.exposeLan ? qsTr(" (LAN)") : ""))
        }
        return values.join("\n")
    }

    function mountsText() {
        if (mountsList.length === 0) {
            return "-"
        }
        const values = []
        for (let i = 0; i < mountsList.length; ++i) {
            const mount = mountsList[i]
            values.push(mount.source + " → " + mount.target
                        + (mount.readOnly ? qsTr(" (somente leitura)") : ""))
        }
        return values.join("\n")
    }

    component MetricCard: Rectangle {
        id: card

        property string label: ""
        property string value: ""
        property string detail: ""

        Layout.fillWidth: true
        implicitHeight: 76
        radius: Theme.radius
        color: Theme.bgSurface2
        border.width: 1
        border.color: Theme.border

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 1

            Text {
                text: card.label
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.5
            }

            Text {
                text: card.value
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontHeading - 3
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
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

    component ChartBox: Rectangle {
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

            Sparkline {
                Layout.fillWidth: true
                Layout.fillHeight: true
                values: chart.values
                maxValue: chart.autoScale ? 0 : 100
                autoScale: chart.autoScale
                lineColor: chart.lineColor
            }
        }
    }

    color: Theme.bgSurface
    radius: Theme.radiusLg
    border.width: 1
    border.color: Theme.border
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 12
            Layout.bottomMargin: 10
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    text: root.machineId.length > 0 ? root.machineName
                                                    : qsTr("Nenhuma machine selecionada")
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.machineId.length > 0
                    text: root.machineId
                    color: Theme.textTertiary
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontMicro
                    elide: Text.ElideMiddle
                }
            }

            AppBadge {
                visible: root.machineId.length > 0
                text: root.stateLabel(root.machineState)
                tone: root.stateTone(root.machineState)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.bottomMargin: 10
            spacing: 7

            ActionButton {
                text: qsTr("Start")
                size: "sm"
                enabled: root.machineId.length > 0 && root.machineState !== "running"
                         && root.machineState !== "starting"
                         && backend.actionActive === false
                onClicked: backend.machineLifecycle("start", root.machineId)
            }

            ActionButton {
                text: qsTr("Stop")
                size: "sm"
                enabled: root.machineId.length > 0
                         && (root.machineState === "running"
                             || root.machineState === "starting")
                         && backend.actionActive === false
                onClicked: backend.machineLifecycle("stop", root.machineId, false)
            }

            ActionButton {
                text: qsTr("SSH")
                iconName: "terminal"
                size: "sm"
                enabled: root.machineId.length > 0 && root.machineState === "running"
                         && backend.actionActive === false
                onClicked: backend.openMachineSsh(root.machineId)
            }

            ActionButton {
                text: qsTr("Restart")
                size: "sm"
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && backend.actionActive === false
                onClicked: backend.machineLifecycle("restart", root.machineId)
            }

            ActionButton {
                text: qsTr("Excluir")
                variant: "danger"
                size: "sm"
                enabled: root.machineId.length > 0 && backend.actionActive === false
                onClicked: root.deleteRequested(root.machineId)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.bottomMargin: 8
            spacing: 7

            Item { Layout.fillWidth: true }

            ActionButton {
                text: qsTr("Info")
                size: "sm"
                variant: root.tab === "info" ? "primary" : "ghost"
                onClicked: root.selectTab("info")
            }

            ActionButton {
                text: qsTr("Logs")
                size: "sm"
                variant: root.tab === "logs" ? "primary" : "ghost"
                onClicked: root.selectTab("logs")
            }

            ActionButton {
                text: qsTr("Stats")
                size: "sm"
                variant: root.tab === "stats" ? "primary" : "ghost"
                onClicked: root.selectTab("stats")
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.tab === "logs" ? 1 : (root.tab === "stats" ? 2 : 0)

            Flickable {
                id: infoFlick

                clip: true
                contentWidth: width
                contentHeight: infoColumn.implicitHeight + 26
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                ColumnLayout {
                    id: infoColumn

                    x: 14
                    y: 12
                    width: infoFlick.width - 28
                    spacing: 12

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 18
                        rowSpacing: 7

                        Text { text: qsTr("Distro"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
                        Text { Layout.fillWidth: true; text: root.machine.distroLabel || "-"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta; elide: Text.ElideRight }

                        Text { text: qsTr("Arch"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
                        Text { Layout.fillWidth: true; text: root.machine.arch || "-"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }

                        Text { text: qsTr("CPUs"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
                        Text { Layout.fillWidth: true; text: root.machine.cpus || "-"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }

                        Text { text: qsTr("Memória"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
                        Text { Layout.fillWidth: true; text: root.formatGiB(root.machine.memoryBytes); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }

                        Text { text: qsTr("Disco"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
                        Text { Layout.fillWidth: true; text: root.formatGiB(root.machine.diskBytes); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }

                        Text { text: qsTr("SSH"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
                        Text { Layout.fillWidth: true; text: root.machine.ssh || "-"; color: Theme.textSecondary; font.family: Theme.monoFamily; font.pixelSize: Theme.fontMeta; elide: Text.ElideRight }

                        Text { text: qsTr("Criada"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
                        Text { Layout.fillWidth: true; text: root.machine.createdAt || "-"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta; elide: Text.ElideRight }

                        Text { text: qsTr("Último boot"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
                        Text { Layout.fillWidth: true; text: root.machine.lastBootAt || "-"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta; elide: Text.ElideRight }

                        Text { text: qsTr("Rede isolada"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
                        Text { Layout.fillWidth: true; text: root.machine.isolateNetwork ? qsTr("sim") : qsTr("não"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }

                        Text { text: qsTr("SSH agent"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
                        Text { Layout.fillWidth: true; text: root.machine.forwardSshAgent ? qsTr("encaminhado") : qsTr("não"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.machine.error !== undefined && String(root.machine.error).length > 0
                        text: qsTr("Erro: ") + String(root.machine.error || "")
                        color: Theme.danger
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        text: qsTr("Portas")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        font.weight: Font.DemiBold
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.portsText()
                        color: Theme.textSecondary
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontMicro
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        text: qsTr("Mounts")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        font.weight: Font.DemiBold
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.mountsText()
                        color: Theme.textSecondary
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontMicro
                        wrapMode: Text.WordWrap
                    }
                }
            }

            ColumnLayout {
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 14
                    Layout.rightMargin: 14
                    Layout.topMargin: 8
                    Layout.bottomMargin: 6
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("console.log")
                            color: Theme.textTertiary
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontMicro
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: backend.machineLogsCapturedAt.length > 0
                            text: qsTr("Atualizado em %1").arg(backend.machineLogsCapturedAt)
                            color: Theme.textDisabled
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMicro
                            elide: Text.ElideRight
                        }
                    }

                    ActionButton {
                        text: qsTr("Copiar")
                        size: "sm"
                        iconName: "copy"
                        enabled: backend.machineLogLines.length > 0
                        onClicked: backend.copyMachineLogs()
                    }

                    ActionButton {
                        text: qsTr("Atualizar")
                        size: "sm"
                        iconName: "refresh"
                        enabled: root.machineId.length > 0
                        onClicked: backend.loadMachineLogs(root.machineId, 200)
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.border
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ScrollView {
                        id: machineLogScroll
                        anchors.fill: parent
                        clip: true
                        ScrollBar.vertical.policy: ScrollBar.AsNeeded

                        TextArea {
                            id: machineLogArea
                            width: machineLogScroll.availableWidth
                            text: backend.machineLogLines.join("\n")
                            color: Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontMeta
                            textFormat: TextEdit.PlainText
                            wrapMode: TextEdit.WrapAnywhere
                            readOnly: true
                            selectByMouse: true
                            selectionColor: Theme.accentSoft
                            selectedTextColor: Theme.textPrimary
                            padding: 8
                            onTextChanged: Qt.callLater(root.scrollLogsToEnd)
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 40
                        horizontalAlignment: Text.AlignHCenter
                        visible: backend.machineLogLines.length === 0
                        text: qsTr("Sem linhas de console.\nA machine ainda não inicializou ou o log está vazio.")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Flickable {
                id: statsFlick

                clip: true
                contentWidth: width
                contentHeight: statsColumn.implicitHeight + 24
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                ColumnLayout {
                    id: statsColumn

                    x: 14
                    y: 12
                    width: statsFlick.width - 28
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

                        MetricCard {
                            label: qsTr("CPU")
                            value: root.formatPercent(root.stats.cpuPercent)
                            detail: qsTr("uso da machine")
                        }

                        MetricCard {
                            label: qsTr("RAM")
                            value: root.formatPercent(root.stats.memPercent)
                            detail: root.formatBytes(root.stats.memUsed) + " / "
                                    + root.formatBytes(root.stats.memTotal)
                        }

                        MetricCard {
                            label: qsTr("Disco")
                            value: root.formatPercent(root.stats.diskPercent)
                            detail: root.formatBytes(root.stats.diskUsed) + " / "
                                    + root.formatBytes(root.stats.diskTotal)
                        }

                        MetricCard {
                            label: qsTr("Rede")
                            value: "↓ " + root.formatRate(root.stats.netRx)
                            detail: "↑ " + root.formatRate(root.stats.netTx)
                        }

                        MetricCard {
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

                        ChartBox {
                            label: qsTr("CPU (%)")
                            values: backend.machineCpuHistory
                            lineColor: Theme.cyan
                        }

                        ChartBox {
                            label: qsTr("RAM (%)")
                            values: backend.machineMemHistory
                            lineColor: Theme.indigo400
                        }

                        ChartBox {
                            label: qsTr("Rede ↓")
                            values: backend.machineNetRxHistory
                            lineColor: Theme.cyan
                            autoScale: true
                        }

                        ChartBox {
                            label: qsTr("Rede ↑")
                            values: backend.machineNetTxHistory
                            lineColor: Theme.indigo400
                            autoScale: true
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: backend.machineStatsError.length > 0
                        text: backend.machineStatsError
                        color: Theme.danger
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !root.hasMachineStats && backend.machineStatsError.length === 0
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
        }
    }

    Connections {
        target: backend

        function onMachineLogsChanged() {
            Qt.callLater(root.scrollLogsToEnd)
        }
    }
}
