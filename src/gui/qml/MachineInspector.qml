import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

// Detalhe de uma machine: Info, Logs, Stats e ações de ciclo de vida.
// No card lateral (showTerminalTab/showFilesTab=false) as abas são
// Info/Logs/Stats; no overlay de detalhe ambos são ligados e as abas Terminal
// e Files são adicionadas. O C++ sobrepõe a sessão SSH sobre
// `terminalPlaceholder`.
Rectangle {
    id: root

    property var machine: ({})
    property string tab: "info"
    property bool showTerminalTab: false
    property bool showFilesTab: false
    property string terminalError: ""
    property string appQuery: ""
    readonly property var visibleApps: {
        const query = root.appQuery.trim().toLowerCase()
        if (query.length === 0) {
            return backend.appItems
        }
        return backend.appItems.filter(function(item) {
            return (item.name || "").toLowerCase().indexOf(query) >= 0
        })
    }

    readonly property int terminalTabIndex: 3
    readonly property int filesTabIndex: 4
    readonly property var tabs: {
        const items = [
            { key: "info", label: qsTr("Info") },
            { key: "logs", label: qsTr("Logs") },
            { key: "stats", label: qsTr("Stats") }
        ]
        if (root.showTerminalTab) {
            items.push({ key: "terminal", label: qsTr("Terminal") })
        }
        if (root.showFilesTab) {
            items.push({ key: "files", label: qsTr("Files") })
        }
        return items
    }

    signal deleteRequested(string id)
    signal exportRequested(string id)
    signal renameRequested(string id)
    signal cloneRequested(string id)
    signal resourcesRequested(string id)
    signal portsRequested(string id)
    signal serialRequested(string id)
    signal usbRequested(string id)
    signal snapshotsRequested(string id)
    signal closeRequested()
    signal expandRequested(string id, int tab)
    signal terminalAreaChanged(rect area, bool visible)
    signal externalTerminalRequested()

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

    // O clique no checkbox quebra o binding de `checked`. Reaplica quando a
    // machine muda para o controle voltar a refletir o estado persistido.
    onMachineChanged: {
        if (forwardAgentCheck) {
            forwardAgentCheck.checked = Qt.binding(function() {
                return root.machine.forwardSshAgent === true
            })
        }
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
        const previousTab = root.tab
        root.tab = nextTab
        if (nextTab === "logs") {
            backend.loadMachineLogs(root.machineId, 200)
        } else if (nextTab === "stats" && root.machineId.length > 0) {
            backend.startMachineStats(root.machineId)
        } else if (nextTab === "files" && previousTab !== "files"
                   && root.machineId.length > 0) {
            if (!backend.machineFilesBusy) {
                backend.refreshMachineFiles(root.machineId, "/")
            }
        }
    }

    // Emite a área do placeholder da aba Terminal para o C++ posicionar o
    // TerminalView sobre ele. Fora da aba (ou sem tamanho) emite visible=false
    // e a sessão continua viva, apenas oculta.
    function syncTerminalArea() {
        if (!root.showTerminalTab || root.machineId.length === 0
                || root.tab !== "terminal" || terminalPlaceholder.width <= 0
                || terminalPlaceholder.height <= 0) {
            root.terminalAreaChanged(Qt.rect(0, 0, 0, 0), false)
            return
        }
        const point = terminalPlaceholder.mapToItem(root, 0, 0)
        root.terminalAreaChanged(Qt.rect(point.x, point.y, terminalPlaceholder.width,
                                         terminalPlaceholder.height), true)
    }

    function openExternalTerminal() {
        root.externalTerminalRequested()
    }

    function close() {
        if (root.machineId.length === 0) {
            return
        }
        backend.stopMachineConsole()
        root.terminalAreaChanged(Qt.rect(0, 0, 0, 0), false)
        root.closeRequested()
    }

    onTabChanged: {
        if (root.tab !== "logs") {
            backend.stopMachineConsole()
        }
        if (root.machineId.length > 0) {
            if (typeof appWindow !== "undefined" && appWindow) {
                appWindow.rememberDetailTab("machine", root.tab)
            }
        }
        root.syncTerminalArea()
    }
    onWidthChanged: root.syncTerminalArea()
    onHeightChanged: root.syncTerminalArea()

    onShowTerminalTabChanged: {
        if (!root.showTerminalTab && root.tab === "terminal") {
            root.selectTab("info")
        }
        root.syncTerminalArea()
    }

    onShowFilesTabChanged: {
        if (!root.showFilesTab && root.tab === "files") {
            root.selectTab("info")
        }
    }

    onMachineIdChanged: {
        backend.stopMachineConsole()
        if (root.tab === "stats") {
            backend.stopMachineStats()
            if (root.machineId.length > 0) {
                backend.startMachineStats(root.machineId)
            }
        }
        if (root.machineId.length === 0 && root.tab !== "info") {
            root.tab = "info"
        }
        root.syncTerminalArea()
    }

    Component.onDestruction: {
        backend.stopMachineConsole()
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

    function serialText() {
        const serial = root.machine && root.machine.serialPorts ? root.machine.serialPorts : []
        if (serial.length === 0) {
            return "-"
        }
        return serial.join("\n")
    }

    function usbText() {
        const devices =
            root.machine && root.machine.usbDevices ? root.machine.usbDevices : []
        const values = []
        for (let i = 0; i < devices.length; ++i) {
            const device = devices[i]
            values.push(String(device.bus) + ":" + String(device.address) + " "
                        + String(device.description || device.vendorId))
        }
        return values.length === 0 ? "-" : values.join("\n")
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
    // O card lateral tem contorno próprio; no overlay ele já vive dentro do
    // card do MachineDetailDialog.
    radius: root.showTerminalTab ? 0 : Theme.radiusLg
    border.width: root.showTerminalTab ? 0 : 1
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

            ActionButton {
                visible: root.showTerminalTab
                text: ""
                iconName: "x"
                size: "sm"
                variant: "ghost"
                onClicked: root.close()
                Accessible.name: qsTr("Fechar detalhe da machine")
            }
        }

        // A barra de ações cresceu com o M4; um RowLayout estourava a largura
        // do card em janelas estreitas e cortava os botões da direita. O Flow
        // quebra em quantas linhas forem necessárias.
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.bottomMargin: 10
            spacing: 7
            flow: Flow.LeftToRight

            ActionButton {
                text: qsTr("Start")
                size: "sm"
                enabled: root.machineId.length > 0 && root.machineState !== "running"
                         && root.machineState !== "starting"
                         && root.machineState !== "provisioning"
                         && backend.actionActive === false
                         && backend.machineCreateActive === false
                onClicked: backend.machineLifecycle("start", root.machineId)
            }

            ActionButton {
                text: qsTr("Stop")
                size: "sm"
                enabled: root.machineId.length > 0
                         && (root.machineState === "running"
                             || root.machineState === "starting"
                             || root.machineState === "provisioning")
                         && backend.actionActive === false
                         && backend.machineCreateActive === false
                onClicked: backend.machineLifecycle("stop", root.machineId, false)
            }

            // No card lateral o SSH abre o detalhe da machine já na aba
            // Terminal; no overlay a aba já está visível e este atalho some.
            ActionButton {
                visible: !root.showTerminalTab
                         && (root.machine.provisioner !== "windows"
                             || root.machine.windowsSsh === true)
                text: qsTr("SSH")
                iconName: "terminal"
                size: "sm"
                enabled: root.machineId.length > 0 && root.machineState === "running"
                         && backend.actionActive === false
                onClicked: root.expandRequested(root.machineId, root.terminalTabIndex)
            }

            // Console gráfico SPICE: apenas machines Windows e com o remote-viewer
            // disponível no host. O viewer é lançado pela GUI, nunca pelo daemon.
            ActionButton {
                visible: root.machine.provisioner === "windows"
                text: qsTr("Console")
                iconName: "external"
                size: "sm"
                enabled: root.machineId.length > 0
                         && (root.machineState === "running"
                             || root.machineState === "provisioning")
                         && backend.actionActive === false
                onClicked: backend.openMachineConsole(root.machineId)
                Accessible.name: qsTr("Abrir o console gráfico da machine")
            }

            ActionButton {
                visible: root.machine.provisioner !== "windows"
                text: qsTr("Abrir app")
                iconName: "external"
                size: "sm"
                enabled: root.machineId.length > 0 && root.machineState === "running"
                         && backend.actionActive === false
                onClicked: {
                    backend.refreshApps(root.machineId)
                    appDialog.open()
                }
                Accessible.name: qsTr("Abrir um app da machine")
            }

            ActionButton {
                visible: !root.showTerminalTab
                text: qsTr("Expandir")
                iconName: "expand"
                size: "sm"
                enabled: root.machineId.length > 0
                onClicked: root.expandRequested(root.machineId, 0)
                Accessible.name: qsTr("Expandir detalhe da machine")
            }

            ActionButton {
                text: qsTr("Restart")
                size: "sm"
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && backend.actionActive === false
                         && backend.machineCreateActive === false
                onClicked: backend.machineLifecycle("restart", root.machineId)
            }

            ActionButton {
                text: qsTr("Exportar")
                size: "sm"
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && backend.machineCreateActive === false
                onClicked: root.exportRequested(root.machineId)
            }

            ActionButton {
                text: qsTr("Renomear")
                size: "sm"
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && backend.machineCreateActive === false
                onClicked: root.renameRequested(root.machineId)
            }

            ActionButton {
                text: qsTr("Clonar")
                size: "sm"
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && backend.machineCreateActive === false
                onClicked: root.cloneRequested(root.machineId)
            }

            ActionButton {
                text: qsTr("Portas")
                size: "sm"
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && backend.actionActive === false
                onClicked: root.portsRequested(root.machineId)
                Accessible.name: qsTr("Gerenciar portas encaminhadas da machine")
            }

            ActionButton {
                text: qsTr("Serial")
                size: "sm"
                visible: !root.showTerminalTab
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && backend.actionActive === false
                onClicked: root.serialRequested(root.machineId)
                Accessible.name: qsTr("Gerenciar portas seriais da machine")
            }

            ActionButton {
                text: qsTr("USB")
                size: "sm"
                visible: !root.showTerminalTab
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && backend.actionActive === false
                onClicked: root.usbRequested(root.machineId)
                Accessible.name: qsTr("Gerenciar dispositivos USB da machine")
            }

            ActionButton {
                text: root.machine && root.machine.audioEnabled ? qsTr("Desativar áudio")
                                                               : qsTr("Ativar áudio")
                size: "sm"
                visible: !root.showTerminalTab
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && backend.actionActive === false
                onClicked: backend.setMachineAudio(root.machineId,
                                                   !(root.machine
                                                     && root.machine.audioEnabled))
                Accessible.name: qsTr("Alternar áudio da machine")
            }

            ActionButton {
                text: qsTr("Snapshots")
                size: "sm"
                // O diálogo vive na MachinesPage; no overlay (detalhe) as demais
                // ações do inspetor também não têm handler.
                visible: !root.showTerminalTab
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && backend.actionActive === false
                onClicked: root.snapshotsRequested(root.machineId)
                Accessible.name: qsTr("Gerenciar snapshots do disco da machine")
            }

            ActionButton {
                text: qsTr("Recursos")
                size: "sm"
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && backend.actionActive === false
                onClicked: root.resourcesRequested(root.machineId)
            }

            ActionButton {
                text: qsTr("Excluir")
                variant: "danger"
                size: "sm"
                enabled: root.machineId.length > 0 && backend.actionActive === false
                         && backend.machineCreateActive === false
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

            Repeater {
                model: root.tabs

                delegate: ActionButton {
                    required property var modelData

                    text: modelData.label
                    size: "sm"
                    variant: root.tab === modelData.key ? "primary" : "ghost"
                    onClicked: root.selectTab(modelData.key)
                }
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
            currentIndex: root.tab === "logs" ? 1
                          : root.tab === "stats" ? 2
                          : root.tab === "terminal" ? 3
                          : root.tab === "files" ? 4 : 0

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

                        Text { text: qsTr("Guest"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
                        Text {
                            Layout.fillWidth: true
                            text: {
                                if (root.machine.guestOs && root.machine.guestOs.productName) {
                                    return root.machine.guestOs.productName
                                }
                                return root.machine.guestReady ? qsTr("pronto") : qsTr("aguardando agente/SSH")
                            }
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMeta
                            elide: Text.ElideRight
                        }

                        Text { text: qsTr("Criada"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
                        Text { Layout.fillWidth: true; text: root.machine.createdAt || "-"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta; elide: Text.ElideRight }

                        Text { text: qsTr("Último boot"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
                        Text { Layout.fillWidth: true; text: root.machine.lastBootAt || "-"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta; elide: Text.ElideRight }

                        Text { text: qsTr("Rede isolada"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
                        Text { Layout.fillWidth: true; text: root.machine.isolateNetwork ? qsTr("sim") : qsTr("não"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                    }

                    FormCheck {
                        id: forwardAgentCheck

                        Layout.fillWidth: true
                        text: qsTr("Encaminhar SSH agent")
                        checked: root.machine.forwardSshAgent === true
                        enabled: root.machineId.length > 0
                                 && root.machineState !== "provisioning"
                                 && root.machineState !== "deleting"
                                 && root.machineState !== "deleted"
                        onToggled: {
                            const current = root.machine.forwardSshAgent === true
                            if (checked === current || root.machineId.length === 0) {
                                return
                            }
                            backend.updateMachine({ "id": root.machineId,
                                                    "forwardSshAgent": checked })
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Aviso: com o agent encaminhado, processos no guest podem usar as chaves do seu SSH agent do host. Ative apenas em machines confiáveis.")
                        color: root.machine.forwardSshAgent ? Theme.warning : Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        wrapMode: Text.WordWrap
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
                        text: qsTr("Serial")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        font.weight: Font.DemiBold
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.serialText()
                        color: Theme.textSecondary
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontMicro
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        text: qsTr("USB")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        font.weight: Font.DemiBold
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.usbText()
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
                            text: backend.machineConsoleActive
                                  ? qsTr("console.log · serial (seguindo)")
                                  : qsTr("console.log")
                            color: Theme.textTertiary
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontMicro
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: backend.machineLogsCapturedAt.length > 0
                                     && !backend.machineConsoleActive
                            text: qsTr("Atualizado em %1").arg(backend.machineLogsCapturedAt)
                            color: Theme.textDisabled
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMicro
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: backend.machineConsoleError.length > 0
                            text: backend.machineConsoleError
                            color: Theme.danger
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMicro
                            elide: Text.ElideRight
                        }
                    }

                    ActionButton {
                        text: qsTr("Copiar")
                        size: "sm"
                        iconName: "copy"
                        enabled: !backend.machineConsoleActive
                                 && backend.machineLogLines.length > 0
                        onClicked: backend.copyMachineLogs()
                    }

                    ActionButton {
                        text: qsTr("Atualizar")
                        size: "sm"
                        iconName: "refresh"
                        enabled: root.machineId.length > 0 && !backend.machineConsoleActive
                        onClicked: backend.loadMachineLogs(root.machineId, 200)
                    }

                    ActionButton {
                        text: backend.machineConsoleActive
                              ? qsTr("Parar console")
                              : qsTr("Seguir console")
                        size: "sm"
                        iconName: backend.machineConsoleActive ? "stop" : "terminal"
                        variant: backend.machineConsoleActive ? "danger" : "secondary"
                        enabled: root.machineId.length > 0
                        onClicked: {
                            if (backend.machineConsoleActive) {
                                backend.stopMachineConsole()
                            } else {
                                backend.followMachineConsole(root.machineId, 65536)
                            }
                        }
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
                            text: (backend.machineConsoleActive
                                   || backend.machineConsoleText.length > 0)
                                  ? backend.machineConsoleText
                                  : backend.machineLogLines.join("\n")
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
                                 && backend.machineConsoleText.length === 0
                        text: backend.machineConsoleActive
                              ? qsTr("Seguindo o console serial. Sem dados ainda.")
                              : qsTr("Sem linhas de console.\nA machine ainda não inicializou ou o log está vazio.")
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

            // Terminal — o QTermWidget é sobreposto pelo C++ (overlay QWidget)
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    anchors.topMargin: 8
                    anchors.bottomMargin: 14
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: root.machine.ssh
                                  ? qsTr("Sessão SSH — %1").arg(String(root.machine.ssh))
                                  : qsTr("Sessão SSH")
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMeta
                            elide: Text.ElideRight
                        }

                        ActionButton {
                            text: qsTr("Nova sessão em janela")
                            iconName: "external"
                            size: "sm"
                            variant: "secondary"
                            enabled: root.machineId.length > 0
                            onClicked: root.openExternalTerminal()
                            Accessible.name: qsTr("Abrir uma nova sessão SSH em janela")
                        }
                    }

                    Item {
                        id: terminalPlaceholder
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        onWidthChanged: root.syncTerminalArea()
                        onHeightChanged: root.syncTerminalArea()

                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 40
                            horizontalAlignment: Text.AlignHCenter
                            visible: root.terminalError.length > 0
                            text: root.terminalError
                            color: Theme.danger
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMicro
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            // Files — file manager da machine via SSH/SCP (somente no overlay).
            MachineFilesView {
                machineId: root.machineId
            }
        }
    }

    AppDialog {
        id: appDialog
        title: qsTr("Abrir app da VM")
        modal: true
        width: 560
        anchors.centerIn: parent
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8

            Text {
                text: qsTr("O cliente abre na sua sessão gráfica; fechar a janela encerra a sessão.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            FormField {
                id: appSearch
                Layout.fillWidth: true
                placeholderText: qsTr("Buscar app…")
                onTextChanged: root.appQuery = text
            }
            Text {
                visible: backend.appsLoading
                text: qsTr("Carregando catálogo…")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
            }
            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: 240
                clip: true
                model: root.visibleApps
                ScrollBar.vertical: ScrollBar {}
                delegate: RowLayout {
                    width: ListView.view.width
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        text: modelData.name || modelData.appId
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMeta
                        elide: Text.ElideRight
                    }
                    ActionButton {
                        text: qsTr("Abrir")
                        size: "sm"
                        onClicked: backend.launchApp(root.machineId, modelData.appId, "auto")
                    }
                }
                Text {
                    visible: root.visibleApps.length === 0 && !backend.appsLoading
                    anchors.centerIn: parent
                    text: qsTr("Nenhum app encontrado.")
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMeta
                }
            }
            Text {
                visible: backend.appSessions.length > 0
                text: qsTr("Sessões abertas")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
            }
            Repeater {
                model: backend.appSessions
                delegate: RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        text: (modelData.appId || "") + " · " + (modelData.backend || "")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMeta
                        elide: Text.ElideRight
                    }
                    ActionButton {
                        text: qsTr("Parar")
                        size: "sm"
                        onClicked: backend.stopApp(root.machineId, modelData.sessionId)
                    }
                }
            }
            Text {
                visible: backend.appsError.length > 0
                text: backend.appsError
                color: Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
        onClosed: backend.closeApps()
    }

    Shortcut {
        sequence: "Esc"
        enabled: root.showTerminalTab && root.machineId.length > 0
        onActivated: root.close()
    }

    Connections {
        target: backend

        function onMachineLogsChanged() {
            Qt.callLater(root.scrollLogsToEnd)
        }

        function onMachineConsoleChanged() {
            if (backend.machineConsoleActive) {
                Qt.callLater(root.scrollLogsToEnd)
            }
        }
    }
}
