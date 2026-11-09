import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

// Detalhe expandido de um container: overlay que ocupa a janela principal.
// As abas usam `containers.inspect` para metadados e streams próprios de
// logs/stats para não disputar a subscription do inspector lateral.
Rectangle {
    id: root

    visible: false
    z: 90
    color: Theme.bgBase
    clip: true
    focus: true

    property string containerId: ""
    property int tabIndex: 0
    property bool showEnvValues: false
    property bool removePending: false
    property bool reloadPending: false

    readonly property bool compact: width < 1180
    readonly property var detail: backend.containerDetail || ({})
    readonly property var config: root.detail.Config || ({})
    readonly property var detailState: root.detail.State || ({})
    readonly property var hostConfig: root.detail.HostConfig || ({})
    readonly property var networkSettings: root.detail.NetworkSettings || ({})
    readonly property var summary: {
        const items = backend.containers || []
        for (let i = 0; i < items.length; ++i) {
            if (String(items[i].id) === root.containerId) {
                return items[i]
            }
        }
        return ({})
    }
    readonly property bool loaded: root.containerId.length > 0
                                  && String(root.detail.Id || "") === root.containerId
    readonly property string containerName: {
        if (root.loaded && root.detail.Name) {
            return String(root.detail.Name).replace(/^\//, "")
        }
        if (root.summary.name) {
            return String(root.summary.name)
        }
        return root.containerId
    }
    readonly property string imageText: {
        if (root.loaded && root.config.Image) {
            return String(root.config.Image)
        }
        return String(root.summary.image || "")
    }
    readonly property string stateText: {
        if (root.loaded && root.detailState.Status) {
            return String(root.detailState.Status)
        }
        return String(root.summary.state || "")
    }
    readonly property string statusText: {
        if (root.loaded) {
            const running = root.detailState.Running === true
            const pid = Number(root.detailState.Pid || 0)
            return running ? qsTr("em execução · PID %1").arg(pid)
                           : qsTr("parado · exit %1").arg(Number(root.detailState.ExitCode || 0))
        }
        return String(root.summary.status || "")
    }
    readonly property bool running: root.stateText === "running"
    readonly property string healthText: {
        if (root.detailState.Health && root.detailState.Health.Status) {
            return String(root.detailState.Health.Status)
        }
        if (root.config.Healthcheck && Object.keys(root.config.Healthcheck).length > 0) {
            return qsTr("configurado")
        }
        return qsTr("sem healthcheck")
    }
    readonly property string restartPolicyText: {
        const policy = root.hostConfig.RestartPolicy || ({})
        const name = String(policy.Name || "")
        if (name.length === 0 || name === "no") {
            return qsTr("sem restart automático")
        }
        const retries = Number(policy.MaximumRetryCount || 0)
        return retries > 0 ? name + qsTr(" (máx. %1)").arg(retries) : name
    }
    readonly property string commandText: {
        const cmd = root.config.Cmd || []
        return Array.isArray(cmd) && cmd.length > 0 ? cmd.join(" ") : "—"
    }
    readonly property string entrypointText: {
        const entrypoint = root.config.Entrypoint || []
        return Array.isArray(entrypoint) && entrypoint.length > 0 ? entrypoint.join(" ") : "—"
    }
    readonly property var infoFields: [
        { label: qsTr("ESTADO"), value: root.stateText.length > 0 ? root.stateText : "—" },
        { label: qsTr("HEALTH"), value: root.healthText },
        { label: qsTr("PID"), value: String(root.detailState.Pid || "—") },
        { label: qsTr("EXIT CODE"), value: String(root.detailState.ExitCode !== undefined
                                                   ? root.detailState.ExitCode : "—") },
        { label: qsTr("CRIADO"), value: String(root.detail.Created || root.summary.created || "—") },
        { label: qsTr("INICIADO"), value: String(root.detailState.StartedAt || "—") },
        { label: qsTr("FINALIZADO"), value: String(root.detailState.FinishedAt || "—") },
        { label: qsTr("RESTART"), value: root.restartPolicyText },
        { label: qsTr("OOM KILLED"), value: root.detailState.OOMKilled === true ? qsTr("sim")
                                                                            : qsTr("não") }
    ]
    readonly property var portLines: {
        const rows = []
        const bindings = root.hostConfig.PortBindings || ({})
        for (const key in bindings) {
            const entries = bindings[key] || []
            for (let i = 0; i < entries.length; ++i) {
                const entry = entries[i] || ({})
                const host = String(entry.HostIp || "0.0.0.0")
                const port = String(entry.HostPort || "")
                rows.push(host + ":" + port + " → " + key)
            }
        }
        if (rows.length === 0) {
            const ports = root.networkSettings.Ports || ({})
            for (const key in ports) {
                const entries = ports[key] || []
                for (let i = 0; i < entries.length; ++i) {
                    const entry = entries[i] || ({})
                    rows.push(String(entry.HostIp || "0.0.0.0") + ":"
                              + String(entry.HostPort || "") + " → " + key)
                }
            }
        }
        return rows
    }
    readonly property var labelLines: {
        const labels = root.config.Labels || ({})
        const rows = []
        for (const key in labels) {
            rows.push(key + "=" + String(labels[key]))
        }
        return rows
    }
    readonly property var envEntries: root.config.Env || []
    readonly property int envCount: root.envEntries.length
    readonly property string envText: {
        const lines = []
        for (let i = 0; i < root.envEntries.length; ++i) {
            const entry = String(root.envEntries[i])
            const separator = entry.indexOf("=")
            const key = separator > 0 ? entry.substring(0, separator) : entry
            if (!root.showEnvValues && root.sensitiveKey(key)) {
                lines.push(key + "=••••••")
            } else {
                lines.push(entry)
            }
        }
        return lines.length > 0 ? lines.join("\n") : qsTr("Nenhuma variável de ambiente definida.")
    }
    readonly property var networkEntries: {
        const networks = root.networkSettings.Networks || ({})
        const rows = []
        for (const key in networks) {
            const network = networks[key] || ({})
            const aliases = network.Aliases || []
            rows.push({
                          name: key,
                          ip: String(network.IPAddress || "—"),
                          gateway: String(network.Gateway || "—"),
                          mac: String(network.MacAddress || "—"),
                          aliases: Array.isArray(aliases) && aliases.length > 0
                                   ? aliases.join(", ") : "—",
                          id: String(network.NetworkID || "—")
                      })
        }
        return rows
    }
    readonly property var mountEntries: root.detail.Mounts || []
    readonly property string inspectText: root.loaded ? JSON.stringify(root.detail, null, 2) : ""

    signal closed()

    function open(id) {
        if (id.length === 0) {
            return
        }
        if (root.visible) {
            backend.stopContainerDetailStats()
            backend.stopContainerDetailLogs()
        }
        root.containerId = id
        root.tabIndex = 0
        root.removePending = false
        root.reloadPending = false
        root.visible = true
        backend.loadContainerDetail(id)
        backend.startContainerDetailStats(id)
        backend.startContainerDetailLogs(id, 200, true)
        root.forceActiveFocus()
    }

    function close() {
        if (!root.visible) {
            return
        }
        backend.stopContainerDetailStats()
        backend.stopContainerDetailLogs()
        backend.clearContainerDetailLogs()
        root.visible = false
        root.closed()
    }

    function lifecycle(action) {
        if (root.containerId.length === 0) {
            return
        }
        root.reloadPending = true
        backend.containerDetailLifecycle(action, root.containerId)
    }

    function sensitiveKey(key) {
        const lower = String(key).toLowerCase()
        return lower.indexOf("token") >= 0 || lower.indexOf("password") >= 0
               || lower.indexOf("secret") >= 0 || lower.indexOf("authorization") >= 0
               || lower.indexOf("key") >= 0
    }

    function stateTone(state) {
        if (state === "running") {
            return "success"
        }
        if (state === "paused") {
            return "warning"
        }
        if (state === "exited" || state === "dead") {
            return "muted"
        }
        return state.length > 0 ? "muted" : "muted"
    }

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

    function copyTextArea(area) {
        if (!area || area.text.length === 0) {
            return
        }
        area.selectAll()
        area.copy()
        area.deselect()
        area.cursorPosition = area.length
    }

    component SectionTitle: Text {
        Layout.fillWidth: true
        Layout.topMargin: 4
        text: ""
        color: Theme.textTertiary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontMicro
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.8
    }

    component InfoRow: RowLayout {
        id: infoRow

        property string label: ""
        property string value: ""

        Layout.fillWidth: true
        spacing: 12

        Text {
            Layout.preferredWidth: 128
            text: infoRow.label
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            text: infoRow.value
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMeta
            wrapMode: Text.WrapAnywhere
        }
    }

    component MetricCard: Rectangle {
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

            Sparkline {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 24
                values: card.values
                lineColor: card.accent
                maxValue: card.maxValue
                autoScale: card.autoScale
            }

            Sparkline {
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

    Shortcut {
        sequence: "Esc"
        enabled: root.visible
        onActivated: root.close()
    }

    Connections {
        target: backend

        function onActionFinished(ok) {
            if (root.removePending) {
                root.removePending = false
                if (ok) {
                    root.close()
                }
                return
            }
            if (!root.reloadPending) {
                return
            }
            root.reloadPending = false
            if (!ok || !root.visible || root.containerId.length === 0) {
                return
            }
            backend.loadContainerDetail(root.containerId)
            backend.startContainerDetailStats(root.containerId)
            backend.startContainerDetailLogs(root.containerId, 200, true)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Cabeçalho
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 88
            color: Theme.bgSurface
            border.width: 0
            border.color: Theme.border

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 10

                ActionButton {
                    text: qsTr("Voltar")
                    variant: "secondary"
                    size: "sm"
                    onClicked: root.close()
                    Accessible.name: qsTr("Voltar e fechar o detalhe expandido")
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 160
                    spacing: 2

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: root.containerName
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontHeading - 3
                            font.weight: Font.DemiBold
                            font.letterSpacing: -0.3
                            elide: Text.ElideRight
                        }

                        AppBadge {
                            text: root.stateText.length > 0 ? root.stateText : "—"
                            tone: root.stateTone(root.stateText)
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.imageText.length > 0 ? root.imageText
                                                        : (root.statusText.length > 0
                                                           ? root.statusText : root.containerId)
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMeta
                        elide: Text.ElideRight
                    }
                }

                ActionButton {
                    visible: !root.running
                    text: root.compact ? "" : qsTr("Iniciar")
                    iconName: "play"
                    variant: "primary"
                    size: "sm"
                    enabled: root.containerId.length > 0 && !backend.actionActive
                    onClicked: root.lifecycle("start")
                    Accessible.name: qsTr("Iniciar container")
                }

                ActionButton {
                    visible: root.running
                    text: root.compact ? "" : qsTr("Parar")
                    iconName: "stop"
                    size: "sm"
                    enabled: root.containerId.length > 0 && !backend.actionActive
                    onClicked: root.lifecycle("stop")
                    Accessible.name: qsTr("Parar container")
                }

                ActionButton {
                    text: root.compact ? "" : qsTr("Reiniciar")
                    iconName: "rotate"
                    size: "sm"
                    enabled: root.containerId.length > 0 && !backend.actionActive
                    onClicked: root.lifecycle("restart")
                    Accessible.name: qsTr("Reiniciar container")
                }

                ActionButton {
                    text: root.compact ? "" : qsTr("Terminal")
                    iconName: "terminal"
                    size: "sm"
                    enabled: root.containerId.length > 0
                    onClicked: backend.openTerminal(root.containerId)
                    Accessible.name: qsTr("Abrir terminal do container")
                }

                ActionButton {
                    text: root.compact ? "" : qsTr("Remover")
                    iconName: "trash"
                    variant: "danger"
                    size: "sm"
                    enabled: root.containerId.length > 0 && !backend.actionActive
                    onClicked: removeDialog.open()
                    Accessible.name: qsTr("Remover container")
                }
            }
        }

        // Abas
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 10
            radius: Theme.radiusSm
            color: Theme.bgSurface2
            border.width: 1
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.margins: 3
                spacing: 2

                Repeater {
                    model: [qsTr("Visão geral"), qsTr("Logs"), qsTr("Info"), qsTr("Ambiente"),
                            qsTr("Redes"), qsTr("Volumes"), qsTr("Inspect")]

                    ItemDelegate {
                        id: detailTab
                        readonly property bool active: root.tabIndex === index

                        Layout.fillWidth: true
                        implicitHeight: 30
                        hoverEnabled: true
                        Accessible.name: modelData

                        background: Rectangle {
                            radius: Theme.radiusXs
                            color: detailTab.active ? Theme.bgElevated
                                                    : detailTab.hovered ? Theme.bgHover
                                                                        : "transparent"
                        }

                        contentItem: Text {
                            text: modelData
                            color: detailTab.active ? Theme.textPrimary : Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMeta
                            font.weight: Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        onClicked: root.tabIndex = index
                    }
                }
            }
        }

        // Erro do inspect
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 8
            Layout.preferredHeight: visible ? 36 : 0
            visible: backend.containerDetailError.length > 0
            radius: Theme.radiusSm
            color: Theme.dangerSoft
            border.width: 1
            border.color: Theme.danger

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                AppIcon {
                    name: "alert"
                    size: 14
                    color: Theme.danger
                }

                Text {
                    Layout.fillWidth: true
                    text: backend.containerDetailError
                    color: Theme.danger
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMeta
                    elide: Text.ElideRight
                }

                ActionButton {
                    text: qsTr("Tentar novamente")
                    size: "sm"
                    variant: "ghost"
                    onClicked: backend.loadContainerDetail(root.containerId)
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 10
            currentIndex: root.tabIndex

            // Visão geral
            ScrollView {
                id: overviewScroll
                clip: true
                contentWidth: availableWidth
                ScrollBar.vertical: ScrollBar {}

                ColumnLayout {
                    x: 20
                    y: 12
                    width: Math.max(0, overviewScroll.availableWidth - 40)
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
                            text: backend.containerDetailLoading ? qsTr("carregando inspect…")
                                                                 : qsTr("stats ~1 s")
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMicro
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.width < 900 ? 1 : 2
                        columnSpacing: 12
                        rowSpacing: 12

                        MetricCard {
                            label: qsTr("CPU")
                            value: backend.containerDetailStats.cpu_percent || "—"
                            detailText: qsTr("histórico de 60 amostras")
                            values: backend.containerDetailCpuHistory
                            accent: Theme.cyan
                            maxValue: 100
                        }

                        MetricCard {
                            label: qsTr("RAM")
                            value: {
                                const stats = backend.containerDetailStats
                                if (Number(stats.memLimitBytes || 0) > 0) {
                                    return root.formatBytes(stats.memUsedBytes) + " / "
                                           + root.formatBytes(stats.memLimitBytes)
                                }
                                return stats.mem_usage || "—"
                            }
                            detailText: backend.containerDetailStats.mem_percent || qsTr("aguardando amostras")
                            values: backend.containerDetailMemHistory
                            accent: Theme.indigo400
                            maxValue: 100
                        }

                        MetricCard {
                            label: qsTr("REDE  ↓ / ↑")
                            value: {
                                const stats = backend.containerDetailStats
                                if (stats.netRxBytes !== undefined) {
                                    return "↓ " + root.formatBytes(stats.netRxBytes) + "   ↑ "
                                           + root.formatBytes(stats.netTxBytes)
                                }
                                return stats.net_io || "—"
                            }
                            detailText: qsTr("RX (ciano) e TX (índigo), acumulados")
                            values: backend.containerDetailNetRxHistory
                            secondaryValues: backend.containerDetailNetTxHistory
                            accent: Theme.cyan
                            autoScale: true
                        }

                        MetricCard {
                            label: qsTr("DISCO  R / W")
                            value: {
                                const stats = backend.containerDetailStats
                                if (stats.diskReadBytes !== undefined) {
                                    return "R " + root.formatBytes(stats.diskReadBytes) + "   W "
                                           + root.formatBytes(stats.diskWriteBytes)
                                }
                                return stats.block_io || "—"
                            }
                            detailText: qsTr("leitura (ciano) e escrita (índigo), acumuladas")
                            values: backend.containerDetailDiskReadHistory
                            secondaryValues: backend.containerDetailDiskWriteHistory
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
                                      .arg(String(backend.containerDetailStats.pids || "—"))
                                      .arg(root.stateText.length > 0 ? root.stateText : "—")
                                      .arg(root.statusText)
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMicro
                            elide: Text.ElideRight
                        }

                        Text {
                            visible: backend.containerDetailStatsError.length > 0
                            text: backend.containerDetailStatsError
                            color: Theme.danger
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMicro
                            elide: Text.ElideRight
                        }
                    }

                    Item { Layout.preferredHeight: 12 }
                }
            }

            // Logs
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    anchors.topMargin: 4
                    anchors.bottomMargin: 16
                    spacing: 6

                    Text {
                        Layout.fillWidth: true
                        visible: backend.containerDetailLogsError.length > 0
                        text: backend.containerDetailLogsError
                        color: Theme.danger
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        wrapMode: Text.WordWrap
                    }

                    LogsView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        lines: backend.containerDetailLogLines
                        emptyText: qsTr("Sem logs. O stream do container será exibido aqui em tempo real.")
                        onClearRequested: backend.clearContainerDetailLogs()
                    }
                }
            }

            // Info
            ScrollView {
                id: infoScroll
                clip: true
                contentWidth: availableWidth
                ScrollBar.vertical: ScrollBar {}

                ColumnLayout {
                    x: 20
                    y: 12
                    width: Math.max(0, infoScroll.availableWidth - 40)
                    spacing: 8

                    SectionTitle { text: qsTr("Geral") }

                    Repeater {
                        model: root.infoFields
                        delegate: InfoRow {
                            label: modelData.label
                            value: modelData.value
                        }
                    }

                    SectionTitle {
                        Layout.topMargin: 14
                        text: qsTr("Imagem e comando")
                    }

                    InfoRow { label: qsTr("IMAGEM"); value: root.imageText.length > 0 ? root.imageText : "—" }
                    InfoRow { label: qsTr("DIGEST"); value: String(root.detail.Image || "—") }
                    InfoRow { label: qsTr("ENTRYPOINT"); value: root.entrypointText }
                    InfoRow { label: qsTr("COMANDO"); value: root.commandText }
                    InfoRow { label: qsTr("WORKDIR"); value: String(root.config.WorkingDir || "—") }
                    InfoRow { label: qsTr("USUÁRIO"); value: String(root.config.User || "—") }
                    InfoRow { label: qsTr("ID"); value: root.containerId }

                    SectionTitle {
                        Layout.topMargin: 14
                        text: qsTr("Portas publicadas")
                    }

                    InfoRow {
                        visible: root.portLines.length === 0
                        label: ""
                        value: qsTr("Nenhuma porta publicada.")
                    }

                    Repeater {
                        model: root.portLines
                        delegate: InfoRow {
                            label: qsTr("PORTA")
                            value: modelData
                        }
                    }

                    SectionTitle {
                        Layout.topMargin: 14
                        text: qsTr("Labels")
                    }

                    InfoRow {
                        visible: root.labelLines.length === 0
                        label: ""
                        value: qsTr("Nenhuma label definida.")
                    }

                    Repeater {
                        model: root.labelLines
                        delegate: Text {
                            Layout.fillWidth: true
                            text: modelData
                            color: Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontMicro
                            wrapMode: Text.WrapAnywhere
                        }
                    }

                    Item { Layout.preferredHeight: 16 }
                }
            }

            // Ambiente
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    anchors.topMargin: 4
                    anchors.bottomMargin: 16
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("%1 variáveis de ambiente").arg(root.envCount)
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMeta
                        }

                        FormCheck {
                            id: showEnv
                            text: qsTr("Mostrar valores")
                            checked: root.showEnvValues
                            onToggled: root.showEnvValues = checked
                        }

                        ActionButton {
                            text: qsTr("Copiar")
                            iconName: "copy"
                            size: "sm"
                            variant: "secondary"
                            enabled: root.envCount > 0
                            onClicked: root.copyTextArea(envArea)
                            Accessible.name: qsTr("Copiar variáveis de ambiente")
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Theme.terminalBg
                        radius: Theme.radiusSm
                        border.width: 1
                        border.color: Theme.border
                        clip: true

                        ScrollView {
                            id: envScroll
                            anchors.fill: parent
                            clip: true

                            TextArea {
                                id: envArea
                                readOnly: true
                                selectByMouse: true
                                persistentSelection: true
                                textFormat: TextEdit.PlainText
                                text: root.envText
                                color: Theme.textSecondary
                                selectionColor: Theme.indigo500
                                selectedTextColor: Theme.textPrimary
                                font.family: Theme.monoFamily
                                font.pixelSize: Theme.fontMeta
                                wrapMode: TextEdit.WrapAnywhere
                                width: envScroll.availableWidth
                                padding: 10
                                background: null
                                Accessible.name: qsTr("Variáveis de ambiente do container")
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Chaves com token, password, secret, authorization ou key "
                                   + "ficam mascaradas até marcar “Mostrar valores”.")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // Redes
            ScrollView {
                id: networksScroll
                clip: true
                contentWidth: availableWidth
                ScrollBar.vertical: ScrollBar {}

                ColumnLayout {
                    x: 20
                    y: 12
                    width: Math.max(0, networksScroll.availableWidth - 40)
                    spacing: 10

                    SectionTitle { text: qsTr("Portas publicadas") }

                    Repeater {
                        model: root.portLines
                        delegate: Text {
                            Layout.fillWidth: true
                            text: modelData
                            color: Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontMeta
                            wrapMode: Text.WrapAnywhere
                        }
                    }

                    Text {
                        visible: root.portLines.length === 0
                        Layout.fillWidth: true
                        text: qsTr("Nenhuma porta publicada.")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMeta
                    }

                    SectionTitle {
                        Layout.topMargin: 10
                        text: qsTr("Interfaces")
                    }

                    Text {
                        visible: root.networkEntries.length === 0
                        Layout.fillWidth: true
                        text: qsTr("Nenhuma rede conectada.")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMeta
                    }

                    Repeater {
                        model: root.networkEntries
                        delegate: Rectangle {
                            id: networkCard

                            Layout.fillWidth: true
                            implicitHeight: networkColumn.implicitHeight + 24
                            radius: Theme.radius
                            color: Theme.bgSurface2
                            border.width: 1
                            border.color: Theme.border

                            ColumnLayout {
                                id: networkColumn
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 5

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.name
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontBody
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    AppBadge {
                                        text: qsTr("rede")
                                        tone: "muted"
                                    }
                                }

                                InfoRow { label: qsTr("IP"); value: modelData.ip }
                                InfoRow { label: qsTr("GATEWAY"); value: modelData.gateway }
                                InfoRow { label: qsTr("MAC"); value: modelData.mac }
                                InfoRow { label: qsTr("ALIASES"); value: modelData.aliases }
                                InfoRow { label: qsTr("NETWORK ID"); value: modelData.id }
                            }
                        }
                    }

                    Item { Layout.preferredHeight: 16 }
                }
            }

            // Volumes
            ScrollView {
                id: volumesScroll
                clip: true
                contentWidth: availableWidth
                ScrollBar.vertical: ScrollBar {}

                ColumnLayout {
                    x: 20
                    y: 12
                    width: Math.max(0, volumesScroll.availableWidth - 40)
                    spacing: 10

                    Text {
                        visible: root.mountEntries.length === 0
                        Layout.fillWidth: true
                        text: qsTr("Nenhum volume ou bind mount.")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMeta
                    }

                    Repeater {
                        model: root.mountEntries
                        delegate: Rectangle {
                            id: mountCard

                            Layout.fillWidth: true
                            implicitHeight: mountColumn.implicitHeight + 24
                            radius: Theme.radius
                            color: Theme.bgSurface2
                            border.width: 1
                            border.color: Theme.border

                            ColumnLayout {
                                id: mountColumn
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 5

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Text {
                                        Layout.fillWidth: true
                                        text: String(modelData.Type || qsTr("mount"))
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontBody
                                        font.weight: Font.DemiBold
                                    }

                                    AppBadge {
                                        text: modelData.RW === false ? qsTr("somente leitura")
                                                                     : qsTr("leitura/escrita")
                                        tone: modelData.RW === false ? "warning" : "success"
                                    }
                                }

                                InfoRow {
                                    label: qsTr("NOME")
                                    value: String(modelData.Name || "—")
                                }
                                InfoRow {
                                    label: qsTr("ORIGEM")
                                    value: String(modelData.Source || "—")
                                }
                                InfoRow {
                                    label: qsTr("DESTINO")
                                    value: String(modelData.Destination || "—")
                                }
                                InfoRow {
                                    label: qsTr("DRIVER")
                                    value: String(modelData.Driver || "—")
                                }
                                InfoRow {
                                    label: qsTr("PROPAGAÇÃO")
                                    value: String(modelData.Propagation || "—")
                                }
                            }
                        }
                    }

                    Item { Layout.preferredHeight: 16 }
                }
            }

            // Inspect
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    anchors.topMargin: 4
                    anchors.bottomMargin: 16
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("JSON completo do Libpod (containers.inspect)")
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMeta
                        }

                        ActionButton {
                            text: qsTr("Copiar")
                            iconName: "copy"
                            size: "sm"
                            variant: "secondary"
                            enabled: root.loaded
                            onClicked: backend.copyContainerInspect()
                            Accessible.name: qsTr("Copiar JSON do inspect")
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Theme.terminalBg
                        radius: Theme.radiusSm
                        border.width: 1
                        border.color: Theme.border
                        clip: true

                        ScrollView {
                            id: inspectScroll
                            anchors.fill: parent
                            clip: true

                            TextArea {
                                id: inspectArea
                                readOnly: true
                                selectByMouse: true
                                persistentSelection: true
                                textFormat: TextEdit.PlainText
                                text: root.inspectText
                                color: Theme.textSecondary
                                selectionColor: Theme.indigo500
                                selectedTextColor: Theme.textPrimary
                                font.family: Theme.monoFamily
                                font.pixelSize: Theme.fontMeta
                                wrapMode: TextEdit.NoWrap
                                width: Math.max(inspectScroll.availableWidth, implicitWidth)
                                padding: 10
                                background: null
                                Accessible.name: qsTr("JSON do inspect do container")
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 32
                            visible: root.inspectText.length === 0
                            horizontalAlignment: Text.AlignHCenter
                            text: backend.containerDetailLoading ? qsTr("Carregando inspect…")
                                                                 : qsTr("Inspect indisponível.")
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMeta
                        }
                    }
                }
            }
        }
    }

    AppDialog {
        id: removeDialog
        title: qsTr("Remover container?")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.border
        }
        contentItem: Text {
            text: qsTr("O container “%1” será removido permanentemente.").arg(root.containerName)
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            wrapMode: Text.WordWrap
        }
        onAccepted: {
            root.removePending = true
            backend.removeContainerDetail(root.containerId, true)
        }
    }
}
