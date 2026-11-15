import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Detalhe de uma machine: Info, Logs, Stats e ações de ciclo de vida.
// No card lateral (showTerminalTab/showFilesTab=false) as abas são
// Info/Logs/Stats; no overlay de detalhe ambos são ligados e as abas Terminal
// e Files são adicionadas. O C++ sobrepõe a sessão SSH sobre o placeholder da
// aba Terminal. Cada aba vive no próprio componente; este arquivo mantém o
// cabeçalho, as ações e o estado de navegação.
Rectangle {
    id: root

    property var machine: ({})
    property string tab: "info"
    property bool showTerminalTab: false
    property bool showFilesTab: false
    property string terminalError: ""

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

    /// O overlay (MachineDetailDialog) só trata excluir e terminal; as demais
    /// ações ficam na página, onde os diálogos existem. Sem isso, os botões
    /// aparecem no overlay e não fazem nada.
    property bool showPageActions: true

    readonly property string machineId: machine && machine.id ? String(machine.id) : ""
    readonly property string machineName: machine && machine.name ? String(machine.name)
                                                                 : machineId
    readonly property string machineState: machine && machine.state ? String(machine.state) : ""
    /// Ação em andamento nesta machine (start/stop/restart/update/…).
    readonly property bool machineBusy: machineId.length > 0
                                        && backend.machines.runningTargets[machineId] !== undefined

    function selectTab(nextTab) {
        if (root.tab === "stats" && nextTab !== "stats") {
            backend.machineDetail.stopStats()
        }
        const previousTab = root.tab
        root.tab = nextTab
        if (nextTab === "logs") {
            backend.machineDetail.loadLogs(root.machineId, 200)
        } else if (nextTab === "stats" && root.machineId.length > 0) {
            backend.machineDetail.startStats(root.machineId)
        } else if (nextTab === "files" && previousTab !== "files"
                   && root.machineId.length > 0) {
            if (!backend.machineDetail.filesBusy) {
                backend.machineDetail.refreshFiles(root.machineId, "/")
            }
        }
    }

    // Emite a área do placeholder da aba Terminal para o C++ posicionar o
    // TerminalView sobre ele. Fora da aba (ou sem tamanho) emite visible=false
    // e a sessão continua viva, apenas oculta.
    function syncTerminalArea() {
        const placeholder = terminalTab.placeholder
        if (!root.showTerminalTab || root.machineId.length === 0
                || root.tab !== "terminal" || !placeholder || placeholder.width <= 0
                || placeholder.height <= 0) {
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
        if (root.machineId.length === 0) {
            return
        }
        backend.machineDetail.stopConsole()
        root.terminalAreaChanged(Qt.rect(0, 0, 0, 0), false)
        root.closeRequested()
    }

    onTabChanged: {
        if (root.tab !== "logs") {
            backend.machineDetail.stopConsole()
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
        backend.machineDetail.stopConsole()
        if (root.tab === "stats") {
            backend.machineDetail.stopStats()
            if (root.machineId.length > 0) {
                backend.machineDetail.startStats(root.machineId)
            }
        }
        if (root.machineId.length === 0 && root.tab !== "info") {
            root.tab = "info"
        }
        root.syncTerminalArea()
    }

    Component.onDestruction: {
        backend.machineDetail.stopConsole()
        if (root.tab === "stats") {
            backend.machineDetail.stopStats()
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

            AppSpinner {
                visible: root.machineBusy
                size: 14
                Layout.alignment: Qt.AlignVCenter
            }

            AppButton {
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

            AppButton {
                text: qsTr("Start")
                size: "sm"
                enabled: root.machineId.length > 0 && root.machineState !== "running"
                         && root.machineState !== "starting"
                         && !root.machineBusy
                         && backend.machines.createActive === false
                onClicked: backend.machines.lifecycle("start", root.machineId)
            }

            AppButton {
                text: qsTr("Stop")
                size: "sm"
                enabled: root.machineId.length > 0
                         && (root.machineState === "running"
                             || root.machineState === "starting")
                         && !root.machineBusy
                         && backend.machines.createActive === false
                onClicked: backend.machines.lifecycle("stop", root.machineId, false)
            }

            // No card lateral o SSH abre o detalhe da machine já na aba
            // Terminal; no overlay a aba já está visível e este atalho some.
            AppButton {
                visible: !root.showTerminalTab
                text: qsTr("SSH")
                iconName: "terminal"
                size: "sm"
                enabled: root.machineId.length > 0 && root.machineState === "running"
                         && !root.machineBusy
                onClicked: root.expandRequested(root.machineId, root.terminalTabIndex)
            }

            AppButton {
                visible: !root.showTerminalTab
                text: qsTr("Expandir")
                iconName: "expand"
                size: "sm"
                enabled: root.machineId.length > 0
                onClicked: root.expandRequested(root.machineId, 0)
                Accessible.name: qsTr("Expandir detalhe da machine")
            }

            AppButton {
                text: qsTr("Restart")
                size: "sm"
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && !root.machineBusy
                         && backend.machines.createActive === false
                onClicked: backend.machines.lifecycle("restart", root.machineId)
            }

            AppButton {
                visible: root.showPageActions
                text: qsTr("Exportar")
                size: "sm"
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && backend.machines.createActive === false
                onClicked: root.exportRequested(root.machineId)
            }

            AppButton {
                visible: root.showPageActions
                text: qsTr("Renomear")
                size: "sm"
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && backend.machines.createActive === false
                onClicked: root.renameRequested(root.machineId)
            }

            AppButton {
                visible: root.showPageActions
                text: qsTr("Clonar")
                size: "sm"
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && backend.machines.createActive === false
                onClicked: root.cloneRequested(root.machineId)
            }

            AppButton {
                visible: root.showPageActions
                text: qsTr("Portas")
                size: "sm"
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && !root.machineBusy
                onClicked: root.portsRequested(root.machineId)
                Accessible.name: qsTr("Gerenciar portas encaminhadas da machine")
            }

            AppButton {
                text: qsTr("Serial")
                size: "sm"
                visible: root.showPageActions && !root.showTerminalTab
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && !root.machineBusy
                onClicked: root.serialRequested(root.machineId)
                Accessible.name: qsTr("Gerenciar portas seriais da machine")
            }

            AppButton {
                text: qsTr("USB")
                size: "sm"
                visible: root.showPageActions && !root.showTerminalTab
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && !root.machineBusy
                onClicked: root.usbRequested(root.machineId)
                Accessible.name: qsTr("Gerenciar dispositivos USB da machine")
            }

            AppButton {
                text: root.machine && root.machine.audioEnabled ? qsTr("Desativar áudio")
                                                               : qsTr("Ativar áudio")
                size: "sm"
                visible: !root.showTerminalTab
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && !root.machineBusy
                onClicked: backend.machines.setAudio(root.machineId,
                                                   !(root.machine
                                                     && root.machine.audioEnabled))
                Accessible.name: qsTr("Alternar áudio da machine")
            }

            AppButton {
                text: qsTr("Snapshots")
                size: "sm"
                // O diálogo vive na MachinesPage; no overlay (detalhe) as demais
                // ações do inspetor também não têm handler.
                visible: root.showPageActions && !root.showTerminalTab
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && !root.machineBusy
                onClicked: root.snapshotsRequested(root.machineId)
                Accessible.name: qsTr("Gerenciar snapshots do disco da machine")
            }

            AppButton {
                visible: root.showPageActions
                text: qsTr("Recursos")
                size: "sm"
                enabled: root.machineId.length > 0
                         && root.machineState !== "provisioning"
                         && root.machineState !== "deleting"
                         && !root.machineBusy
                onClicked: root.resourcesRequested(root.machineId)
            }

            AppButton {
                text: qsTr("Excluir")
                variant: "danger"
                size: "sm"
                enabled: root.machineId.length > 0 && !root.machineBusy
                         && backend.machines.createActive === false
                onClicked: root.deleteRequested(root.machineId)
            }
        }

        AppTabs {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.bottomMargin: 8
            model: root.tabs.map(function(entry) { return entry.label })
            currentIndex: Math.max(0, root.tabs.findIndex(function(entry) {
                return entry.key === root.tab
            }))
            onActivated: function(index) { root.selectTab(root.tabs[index].key) }
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

            InfoTab {
                machine: root.machine
                machineId: root.machineId
                machineState: root.machineState
            }

            LogsTab {
                machineId: root.machineId
            }

            StatsTab {
                machineState: root.machineState
            }

            TerminalTab {
                id: terminalTab
                machine: root.machine
                machineId: root.machineId
                terminalError: root.terminalError
                onExternalTerminalRequested: root.openExternalTerminal()
                onAreaChanged: root.syncTerminalArea()
            }

            FilesTab {
                machineId: root.machineId
            }
        }
    }

    Shortcut {
        sequence: "Esc"
        enabled: root.showTerminalTab && root.machineId.length > 0
        onActivated: root.close()
    }
}
