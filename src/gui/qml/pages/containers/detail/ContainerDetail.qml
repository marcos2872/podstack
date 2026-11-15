import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Conteúdo do detalhe de um container, hospedado diretamente no card do
// ContainerDetailDialog (overlay integrado à MainWindow). As abas usam
// `containers.inspect` para metadados e streams próprios de logs/stats,
// separados do stream compartilhado do Backend. O shell mantém o cabeçalho,
// as ações e o estado compartilhado; cada aba vive no próprio componente.
Rectangle {
    id: root

    color: Theme.bgBase
    clip: true

    readonly property int terminalTabIndex: 7

    property string containerId: ""
    property int tabIndex: 0
    property bool showEnvValues: false
    property bool removePending: false
    property bool reloadPending: false

    readonly property bool compact: width < 1180
    readonly property var detail: backend.containerDetail.detail || ({})
    readonly property var config: root.detail.Config || ({})
    readonly property var detailState: root.detail.State || ({})
    readonly property var hostConfig: root.detail.HostConfig || ({})
    readonly property var networkSettings: root.detail.NetworkSettings || ({})
    readonly property var summary: {
        const items = backend.containers.items || []
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

    signal closeRequested()
    signal terminalAreaChanged(rect area, bool visible)
    signal externalTerminalRequested()

    Shortcut {
        sequence: "Esc"
        enabled: root.containerId.length > 0 && !removeDialog.opened
        onActivated: root.close()
    }

    function open(id) {
        if (id.length === 0) {
            return
        }
        backend.containerDetail.stopStats()
        backend.containerDetail.stopLogs()
        backend.containerDetail.clearLogs()
        root.containerId = id
        root.tabIndex = 0
        root.removePending = false
        root.reloadPending = false
        root.terminalAreaChanged(Qt.rect(0, 0, 0, 0), false)
        backend.containerDetail.load(id)
        backend.containerDetail.startStats(id)
        backend.containerDetail.startLogs(id, 200, true)
    }

    onTabIndexChanged: {
        if (root.tabIndex === 6 && root.containerId.length > 0) {
            backend.containers.listFiles(root.containerId, "/")
        }
        if (root.containerId.length > 0) {
            if (typeof appWindow !== "undefined" && appWindow) {
                appWindow.rememberDetailTab("container", root.tabIndex)
            }
        }
        root.syncTerminalArea()
    }

    onWidthChanged: root.syncTerminalArea()
    onHeightChanged: root.syncTerminalArea()

    // Emite a área do placeholder da aba Terminal para o C++ posicionar o
    // QTermWidget sobre ele. Fora da aba (ou sem tamanho) emite visible=false
    // e a sessão continua viva, apenas oculta.
    function syncTerminalArea() {
        const placeholder = terminalTab.placeholder
        if (root.containerId.length === 0 || root.tabIndex !== root.terminalTabIndex
                || !placeholder || placeholder.width <= 0 || placeholder.height <= 0) {
            root.terminalAreaChanged(Qt.rect(0, 0, 0, 0), false)
            return
        }
        const point = placeholder.mapToItem(root, 0, 0)
        root.terminalAreaChanged(Qt.rect(point.x, point.y, placeholder.width,
                                         placeholder.height), true)
    }

    function openExternalTerminal() {
        root.externalTerminalRequested()
    }

    function close() {
        if (root.containerId.length === 0) {
            return
        }
        root.terminalAreaChanged(Qt.rect(0, 0, 0, 0), false)
        backend.containerDetail.stopStats()
        backend.containerDetail.stopLogs()
        backend.containerDetail.clearLogs()
        root.closeRequested()
    }

    function lifecycle(action) {
        if (root.containerId.length === 0) {
            return
        }
        root.reloadPending = true
        backend.containerDetail.lifecycle(action, root.containerId)
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
            if (!ok || root.containerId.length === 0) {
                return
            }
            backend.containerDetail.load(root.containerId)
            backend.containerDetail.startStats(root.containerId)
            backend.containerDetail.startLogs(root.containerId, 200, true)
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

                AppButton {
                    visible: !root.running
                    text: root.compact ? "" : qsTr("Iniciar")
                    iconName: "play"
                    variant: "primary"
                    size: "sm"
                    enabled: root.containerId.length > 0 && !backend.containers.items.actionActive
                    onClicked: root.lifecycle("start")
                    Accessible.name: qsTr("Iniciar container")
                }

                AppButton {
                    visible: root.running
                    text: root.compact ? "" : qsTr("Parar")
                    iconName: "stop"
                    size: "sm"
                    enabled: root.containerId.length > 0 && !backend.containers.items.actionActive
                    onClicked: root.lifecycle("stop")
                    Accessible.name: qsTr("Parar container")
                }

                AppButton {
                    text: root.compact ? "" : qsTr("Reiniciar")
                    iconName: "rotate"
                    size: "sm"
                    enabled: root.containerId.length > 0 && !backend.containers.items.actionActive
                    onClicked: root.lifecycle("restart")
                    Accessible.name: qsTr("Reiniciar container")
                }

                AppButton {
                    text: root.compact ? "" : qsTr("Remover")
                    iconName: "trash"
                    variant: "danger"
                    size: "sm"
                    enabled: root.containerId.length > 0 && !backend.containers.items.actionActive
                    onClicked: removeDialog.open()
                    Accessible.name: qsTr("Remover container")
                }

                AppButton {
                    text: ""
                    iconName: "x"
                    variant: "ghost"
                    size: "sm"
                    onClicked: root.close()
                    Accessible.name: qsTr("Fechar")
                }
            }
        }

        // Abas
        AppTabs {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 10
            model: [qsTr("Visão geral"), qsTr("Logs"), qsTr("Info"), qsTr("Ambiente"),
                    qsTr("Redes"), qsTr("Volumes"), qsTr("Arquivos"), qsTr("Terminal"),
                    qsTr("Inspect")]
            currentIndex: root.tabIndex
            onActivated: function(index) { root.tabIndex = index }
        }

        // Erro do inspect
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 8
            Layout.preferredHeight: visible ? 36 : 0
            visible: backend.containerDetail.detail.error.length > 0
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
                    text: backend.containerDetail.detail.error
                    color: Theme.danger
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMeta
                    elide: Text.ElideRight
                }

                AppButton {
                    text: qsTr("Tentar novamente")
                    size: "sm"
                    variant: "ghost"
                    onClicked: backend.containerDetail.load(root.containerId)
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 10
            currentIndex: root.tabIndex

            OverviewTab {
                stateText: root.stateText
                statusText: root.statusText
                metricColumns: root.width < 900 ? 1 : 2
            }

            LogsTab {}

            InfoTab {
                infoFields: root.infoFields
                portLines: root.portLines
                labelLines: root.labelLines
                imageText: root.imageText
                entrypointText: root.entrypointText
                commandText: root.commandText
                config: root.config
                detail: root.detail
                containerId: root.containerId
            }

            EnvironmentTab {
                envCount: root.envCount
                envText: root.envText
                showEnvValues: root.showEnvValues
                onShowValuesToggled: function(value) { root.showEnvValues = value }
            }

            NetworksTab {
                portLines: root.portLines
                networkEntries: root.networkEntries
            }

            VolumesTab {
                mountEntries: root.mountEntries
            }

            FilesTab {
                containerId: root.containerId
            }

            TerminalTab {
                id: terminalTab
                containerId: root.containerId
                onExternalTerminalRequested: root.openExternalTerminal()
                onAreaChanged: root.syncTerminalArea()
            }

            InspectTab {
                inspectText: root.inspectText
                loaded: root.loaded
            }
        }
    }

    AppModal {
        id: removeDialog
        title: qsTr("Remover container?")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        // O terminal nativo ficaria sobre o diálogo QML; oculta a sessão
        // enquanto o modal estiver aberto e reaplica a área ao fechar.
        onOpened: root.terminalAreaChanged(Qt.rect(0, 0, 0, 0), false)
        onClosed: root.syncTerminalArea()
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
            backend.containerDetail.remove(root.containerId, true)
        }
    }
}
