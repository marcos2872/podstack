import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: machinesPage

    property string selectedMachineId: ""
    property string pendingDeleteId: ""
    property string pendingExportId: ""
    property string pendingRenameId: ""
    property string pendingCloneId: ""
    property string pendingResourcesId: ""
    property string pendingPortsId: ""
    property bool portsActionPending: false
    property var portsDraft: []
    property string pendingSerialId: ""
    property bool serialActionPending: false
    property bool resourcesActionPending: false
    property int resourcesOriginalCpus: 0
    property int resourcesOriginalMemoryGiB: 0
    property int resourcesOriginalDiskGiB: 0
    property string pendingSnapshotsId: ""
    property bool snapshotActionPending: false
    property string pendingSnapshotAction: ""
    property string pendingSnapshotName: ""

    signal machineExpandRequested(string id, int tab)

    readonly property int machineCount: backend.machines.length
    readonly property int runningCount: {
        let count = 0
        for (let i = 0; i < backend.machines.length; ++i) {
            if (backend.machines[i].state === "running") {
                ++count
            }
        }
        return count
    }

    readonly property var selectedMachine: {
        if (selectedMachineId.length === 0) {
            return ({})
        }
        const info = backend.machineInfo
        let listItem = null
        for (let i = 0; i < backend.machines.length; ++i) {
            if (String(backend.machines[i].id) === selectedMachineId) {
                listItem = backend.machines[i]
                break
            }
        }
        if (info && String(info.id || "") === selectedMachineId) {
            if (!listItem) {
                return info
            }
            // A lista é atualizada logo após start/stop; ela manda no estado
            // para os botões do inspetor não ficarem presos no estado antigo.
            const merged = {}
            for (const key in info) {
                merged[key] = info[key]
            }
            merged.state = listItem.state
            merged.error = listItem.error
            merged.lastBootAt = listItem.lastBootAt
            merged.updatedAt = listItem.updatedAt
            return merged
        }
        return listItem || ({})
    }

    readonly property var distroLabels: {
        const labels = []
        for (let i = 0; i < backend.machineDistros.length; ++i) {
            const distro = backend.machineDistros[i]
            labels.push(distro.label || distro.distro || "")
        }
        return labels
    }

    function selectMachine(id) {
        selectedMachineId = id
        if (id.length > 0) {
            backend.loadMachineInfo(id)
            backend.loadMachineLogs(id, 200)
        }
    }

    function syncSelection() {
        const items = backend.machines
        let index = -1
        for (let i = 0; i < items.length; ++i) {
            if (String(items[i].id) === selectedMachineId) {
                index = i
                break
            }
        }
        if (index < 0 && items.length > 0) {
            index = 0
            for (let i = 0; i < items.length; ++i) {
                if (items[i].state === "running") {
                    index = i
                    break
                }
            }
            selectedMachineId = String(items[index].id)
        }
        if (items.length === 0) {
            selectedMachineId = ""
        }
        machineList.currentRow = index
        if (index >= 0) {
            backend.loadMachineInfo(selectedMachineId)
            backend.loadMachineLogs(selectedMachineId, 200)
        }
    }

    function openPortsDialog(id) {
        pendingPortsId = id
        portsActionPending = false
        portsDraft = []
        let machine = selectedMachine
        for (let i = 0; i < backend.machines.length; ++i) {
            if (String(backend.machines[i].id) === id) {
                machine = backend.machines[i]
                break
            }
        }
        const ports = machine && machine.ports ? machine.ports : []
        const draft = []
        for (let i = 0; i < ports.length; ++i) {
            const port = ports[i]
            const protocol = port.protocol ? String(port.protocol) : "tcp"
            if (protocol === "tcp" && Number(port.guestPort) === 22) {
                continue
            }
            draft.push({
                "id": port.id ? String(port.id) : "",
                "hostPort": Number(port.hostPort || 0),
                "guestPort": Number(port.guestPort || 0),
                "protocol": protocol,
                "exposeLan": Boolean(port.exposeLan),
                "label": port.label ? String(port.label) : ""
            })
        }
        portsDraft = draft
        portsHost.text = ""
        portsGuest.text = ""
        portsProtocol.currentIndex = 0
        portsLabel.text = ""
        portsLan.checked = false
        portsError.text = ""
        portsDialog.open()
    }

    function openSerialDialog(id) {
        pendingSerialId = id
        serialActionPending = false
        serialError.text = ""
        backend.refreshMachineSerialPorts(id)
        backend.loadMachineInfo(id)
        serialDialog.open()
    }

    function attachSerialPort(path) {
        if (pendingSerialId.length === 0 || serialActionPending) {
            return
        }
        serialActionPending = true
        serialError.text = ""
        backend.attachMachineSerialPort(pendingSerialId, path)
    }

    function detachSerialPort(path) {
        if (pendingSerialId.length === 0 || serialActionPending) {
            return
        }
        serialActionPending = true
        serialError.text = ""
        backend.detachMachineSerialPort(pendingSerialId, path)
    }

    function addPortDraft() {        const hostPort = parseInt(portsHost.text)
        const guestPort = parseInt(portsGuest.text)
        if (isNaN(hostPort) || hostPort < 1 || hostPort > 65535) {
            portsError.text = qsTr("Informe a porta do host (1..65535).")
            return
        }
        if (isNaN(guestPort) || guestPort < 1 || guestPort > 65535) {
            portsError.text = qsTr("Informe a porta do guest (1..65535).")
            return
        }
        const protocol = portsProtocol.currentText
        for (let i = 0; i < portsDraft.length; ++i) {
            if (Number(portsDraft[i].hostPort) === hostPort
                    && String(portsDraft[i].protocol) === protocol) {
                portsError.text = qsTr("Essa porta do host já está na lista para o protocolo.")
                return
            }
        }
        portsDraft = portsDraft.concat([{
            "id": "",
            "hostPort": hostPort,
            "guestPort": guestPort,
            "protocol": protocol,
            "exposeLan": portsLan.checked,
            "label": portsLabel.text.trim()
        }])
        portsHost.text = ""
        portsGuest.text = ""
        portsLabel.text = ""
        portsLan.checked = false
        portsError.text = ""
    }

    function removePortDraft(index) {
        const next = []
        for (let i = 0; i < portsDraft.length; ++i) {
            if (i !== index) {
                next.push(portsDraft[i])
            }
        }
        portsDraft = next
    }

    function snapshotSize(value) {
        const number = Number(value || 0)
        if (number <= 0) {
            return "-"
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

    function requestSnapshotAction(action, name) {
        pendingSnapshotAction = action
        pendingSnapshotName = name
        snapshotsError.text = ""
    }

    function confirmSnapshotAction() {
        if (pendingSnapshotsId.length === 0 || pendingSnapshotName.length === 0) {
            return
        }
        snapshotActionPending = true
        snapshotsError.text = ""
        if (pendingSnapshotAction === "restore") {
            backend.restoreMachineSnapshot(pendingSnapshotsId, pendingSnapshotName)
        } else if (pendingSnapshotAction === "remove") {
            backend.removeMachineSnapshot(pendingSnapshotsId, pendingSnapshotName)
        }
    }

    function createSnapshot() {
        const name = snapshotsName.text.trim()
        if (name.length === 0) {
            snapshotsError.text = qsTr("Informe o nome do snapshot.")
            return
        }
        snapshotActionPending = true
        pendingSnapshotAction = "create"
        snapshotsError.text = ""
        backend.createMachineSnapshot(pendingSnapshotsId, name)
    }

    function stageLabel(stage) {
        if (stage === "validating") {
            return qsTr("Validando…")
        }
        if (stage === "image") {
            return qsTr("Baixando imagem cloud…")
        }
        if (stage === "disk") {
            return qsTr("Criando disco qcow2…")
        }
        if (stage === "seed") {
            return qsTr("Gerando seed cloud-init…")
        }
        if (stage === "starting") {
            return qsTr("Iniciando QEMU…")
        }
        if (stage === "running") {
            return qsTr("Aguardando a machine ficar pronta…")
        }
        if (stage === "cancelling") {
            return qsTr("Cancelando…")
        }
        if (stage === "cancelled") {
            return qsTr("Criação cancelada.")
        }
        if (stage === "done") {
            return qsTr("Machine criada.")
        }
        return stage
    }

    function archiveStageLabel(stage) {
        if (stage === "validating") {
            return qsTr("Preparando…")
        }
        if (stage === "stopping") {
            return qsTr("Parando a machine…")
        }
        if (stage === "disk") {
            return qsTr("Preparando o disco…")
        }
        if (stage === "copy") {
            return qsTr("Copiando chaves e seed…")
        }
        if (stage === "archive") {
            return qsTr("Compactando o arquivo…")
        }
        if (stage === "starting") {
            return qsTr("Reiniciando a machine…")
        }
        if (stage === "verify") {
            return qsTr("Verificando o arquivo…")
        }
        if (stage === "metadata") {
            return qsTr("Restaurando a machine…")
        }
        if (stage === "cancelling") {
            return qsTr("Cancelando…")
        }
        if (stage === "cancelled") {
            return qsTr("Operação cancelada.")
        }
        if (stage === "done") {
            return qsTr("Concluído.")
        }
        if (stage === "error") {
            return qsTr("Falhou.")
        }
        return stage
    }

    function createRequested() {        const index = distroSelect.currentIndex
        const distro = index >= 0 && index < backend.machineDistros.length
                       ? backend.machineDistros[index] : ({})
        backend.createMachine({
            name: machineName.text,
            distro: distro.distro || "",
            version: distro.version || "",
            cpus: Number(machineCpus.text || 2),
            memoryGiB: Number(machineMemory.text || 4),
            diskGiB: Number(machineDisk.text || 20),
            user: machineUser.text,
            password: machinePassword.text,
            userData: machineUserData.text,
            mountsText: machineMounts.text,
            isolated: isolatedBox.checked,
            isolateNetwork: isolateNetworkBox.checked,
            forwardSshAgent: forwardSshAgentBox.checked
        })
    }

    spacing: 0

    SelectionState { id: machineSelection }

    ViewHeader {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.topMargin: 20
        Layout.bottomMargin: 16
        title: qsTr("Machines")
        subtitle: machinesPage.runningCount + qsTr(" de ") + machinesPage.machineCount
                  + qsTr(" em execução")

        ActionButton {
            text: qsTr("Nova machine")
            variant: "primary"
            iconName: "plus"
            enabled: !backend.machineCreateActive
            onClicked: {
                machineName.text = ""
                machineCpus.text = "2"
                machineMemory.text = "4"
                machineDisk.text = "20"
                machineUser.text = ""
                machinePassword.text = ""
                machineUserData.text = ""
                machineMounts.text = ""
                isolatedBox.checked = false
                isolateNetworkBox.checked = false
                forwardSshAgentBox.checked = false
                distroSelect.currentIndex = 0
                backend.resetMachineCreateState()
                backend.refreshMachineDistros()
                createDialog.open()
            }
        }

        ActionButton {
            text: qsTr("Importar")
            variant: "ghost"
            iconName: "download"
            enabled: !backend.machineCreateActive
            onClicked: {
                importPath.text = ""
                importName.text = ""
                importForce.checked = false
                backend.resetMachineCreateState()
                importDialog.open()
            }
            Accessible.name: qsTr("Importar machine de um arquivo .tar.zst")
        }

        Text {
            visible: machineSelection.count > 0
            text: machineSelection.count + qsTr(" selecionadas")
            color: Theme.indigo400
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMeta
        }

        ActionButton {
            visible: machineSelection.count > 0
            text: qsTr("Iniciar")
            iconName: "play"
            onClicked: backend.batchMachines(machineSelection.ids(), "start")
        }

        ActionButton {
            visible: machineSelection.count > 0
            text: qsTr("Parar")
            iconName: "stop"
            onClicked: backend.batchMachines(machineSelection.ids(), "stop")
        }

        ActionButton {
            visible: machineSelection.count > 0
            text: qsTr("Remover")
            variant: "danger"
            iconName: "trash"
            onClicked: backend.batchMachines(machineSelection.ids(), "delete", true)
        }

        ActionButton {
            variant: "ghost"
            iconName: "refresh"
            onClicked: backend.refreshMachines()
            Accessible.name: qsTr("Atualizar machines")
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 20
        spacing: 16

        CatalogList {
            id: machineList

            Layout.preferredWidth: 470
            Layout.minimumWidth: 340
            Layout.fillHeight: true
            emptyIcon: "machines"
            emptyText: qsTr("Nenhuma machine.\nCrie uma machine Linux para começar.")
            items: backend.machines
            selection: machineSelection
            idKey: "id"
            columns: [
                { key: "name", label: "NOME", width: 0 },
                { key: "distroLabel", label: "DISTRO", width: 110 },
                { key: "state", label: "ESTADO", width: 90 },
                { key: "ssh", label: "SSH", width: 150, mono: true }
            ]

            onActivated: function(index) {
                if (index >= 0 && index < backend.machines.length) {
                    machinesPage.selectMachine(String(backend.machines[index].id))
                }
            }
        }

        MachineInspector {
            Layout.fillWidth: true
            Layout.minimumWidth: 300
            Layout.fillHeight: true
            machine: machinesPage.selectedMachine
            onExpandRequested: function(id, tab) {
                machinesPage.machineExpandRequested(id, tab)
            }
            onDeleteRequested: function(id) {
                machinesPage.pendingDeleteId = id
                deleteMachineDialog.open()
            }
            onExportRequested: function(id) {
                machinesPage.pendingExportId = id
                exportPath.text = backend.machineArchivePath(id)
                exportForce.checked = false
                backend.resetMachineCreateState()
                exportDialog.open()
            }
            onRenameRequested: function(id) {
                machinesPage.pendingRenameId = id
                renameName.text = ""
                renameDialog.open()
            }
            onCloneRequested: function(id) {
                machinesPage.pendingCloneId = id
                cloneName.text = ""
                backend.resetMachineCreateState()
                cloneDialog.open()
            }
            onPortsRequested: function(id) {
                machinesPage.openPortsDialog(id)
            }
            onSerialRequested: function(id) {
                machinesPage.openSerialDialog(id)
            }
            onSnapshotsRequested: function(id) {
                machinesPage.pendingSnapshotsId = id
                machinesPage.snapshotActionPending = false
                machinesPage.pendingSnapshotAction = ""
                machinesPage.pendingSnapshotName = ""
                snapshotsName.text = ""
                snapshotsError.text = ""
                backend.refreshMachineSnapshots(id)
                snapshotsDialog.open()
            }
            onResourcesRequested: function(id) {
                machinesPage.pendingResourcesId = id
                machinesPage.resourcesActionPending = false
                let machine = machinesPage.selectedMachine
                for (let i = 0; i < backend.machines.length; ++i) {
                    if (String(backend.machines[i].id) === id) {
                        machine = backend.machines[i]
                        break
                    }
                }
                const gib = 1073741824
                machinesPage.resourcesOriginalCpus = Number(machine.cpus || 0)
                machinesPage.resourcesOriginalMemoryGiB = Math.max(1, Math.round(
                    Number(machine.memoryBytes || 0) / gib))
                machinesPage.resourcesOriginalDiskGiB = Math.max(1, Math.round(
                    Number(machine.diskBytes || 0) / gib))
                resourcesCpus.text = String(machinesPage.resourcesOriginalCpus
                                            || 2)
                resourcesMemory.text = String(
                    machinesPage.resourcesOriginalMemoryGiB)
                resourcesDisk.text = String(
                    machinesPage.resourcesOriginalDiskGiB)
                resourcesError.text = ""
                resourcesDialog.open()
            }
        }
    }

    Connections {
        target: backend

        function onMachinesChanged() {
            machinesPage.syncSelection()
        }

        function onMachineCreateStateChanged() {
            if (backend.machineCreateActive || backend.machineCreateStage !== "done") {
                return
            }
            if (backend.machineJobKind === "create") {
                createDialog.close()
            } else if (backend.machineJobKind === "export") {
                exportDialog.close()
            } else if (backend.machineJobKind === "import") {
                importDialog.close()
            } else if (backend.machineJobKind === "clone") {
                cloneDialog.close()
            }
        }

        function onActionFinished(ok, message) {
            if (machinesPage.snapshotActionPending && snapshotsDialog.opened) {
                const action = machinesPage.pendingSnapshotAction
                machinesPage.snapshotActionPending = false
                if (ok) {
                    machinesPage.pendingSnapshotAction = ""
                    machinesPage.pendingSnapshotName = ""
                    snapshotsError.text = ""
                    if (action === "create") {
                        snapshotsName.text = ""
                    }
                } else {
                    snapshotsError.text = message.length > 0 ? message : backend.lastError
                }
                return
            }
            if (machinesPage.serialActionPending) {
                machinesPage.serialActionPending = false
                if (ok) {
                    serialError.text = ""
                    backend.refreshMachineSerialPorts(machinesPage.pendingSerialId)
                    backend.loadMachineInfo(machinesPage.pendingSerialId)
                } else {
                    serialError.text = message.length > 0 ? message : backend.lastError
                }
                if (!serialDialog.opened) {
                    machinesPage.pendingSerialId = ""
                }
                return
            }
            if (machinesPage.portsActionPending && portsDialog.opened) {
                machinesPage.portsActionPending = false
                if (ok) {
                    portsDialog.close()
                } else {
                    portsError.text = message.length > 0 ? message : backend.lastError
                }
                return
            }
            if (!machinesPage.resourcesActionPending || !resourcesDialog.opened) {
                return
            }
            machinesPage.resourcesActionPending = false
            if (ok) {
                resourcesDialog.close()
            } else {
                resourcesError.text = message.length > 0 ? message : backend.lastError
            }
        }
    }

    AppDialog {
        id: createDialog
        title: qsTr("Nova machine")
        modal: true
        width: 580
        closePolicy: backend.machineCreateActive
                     ? Popup.NoAutoClose
                     : Popup.CloseOnEscape | Popup.CloseOnPressOutside

        contentItem: ColumnLayout {
            spacing: 9

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 7

                Text { text: qsTr("Nome"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: machineName
                    Layout.fillWidth: true
                    placeholderText: "dev"
                    enabled: !backend.machineCreateActive
                }

                Text { text: qsTr("Distro"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                AppSelect {
                    id: distroSelect
                    Layout.fillWidth: true
                    model: machinesPage.distroLabels
                    enabled: !backend.machineCreateActive
                    currentIndex: model.length > 0 ? 0 : -1
                }

                Text { text: qsTr("CPUs"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: machineCpus
                    Layout.fillWidth: true
                    text: "2"
                    enabled: !backend.machineCreateActive
                    validator: IntValidator { bottom: 1; top: 256 }
                }

                Text { text: qsTr("Memória (GiB)"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: machineMemory
                    Layout.fillWidth: true
                    text: "4"
                    enabled: !backend.machineCreateActive
                    validator: IntValidator { bottom: 1; top: 1024 }
                }

                Text { text: qsTr("Disco (GiB)"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: machineDisk
                    Layout.fillWidth: true
                    text: "20"
                    enabled: !backend.machineCreateActive
                    validator: IntValidator { bottom: 1; top: 4096 }
                }

                Text { text: qsTr("Usuário"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: machineUser
                    Layout.fillWidth: true
                    placeholderText: qsTr("usuário padrão do host")
                    enabled: !backend.machineCreateActive
                }

                Text { text: qsTr("Senha do usuário"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: machinePassword
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    placeholderText: qsTr("opcional; gravada só como hash")
                    enabled: !backend.machineCreateActive
                }
            }

            Text {
                text: qsTr("user-data (cloud-config)")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
            }

            TextArea {
                id: machineUserData
                Layout.fillWidth: true
                Layout.preferredHeight: 84
                enabled: !backend.machineCreateActive
                placeholderText: "#cloud-config\nruncmd:\n  - echo pronto"
                color: Theme.textPrimary
                placeholderTextColor: Theme.textTertiary
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontMeta
                wrapMode: TextArea.WrapAnywhere
                selectByMouse: true
                selectionColor: Theme.accentSoft
                selectedTextColor: Theme.textPrimary

                background: Rectangle {
                    color: Theme.bgSurface2
                    radius: Theme.radiusSm
                    border.width: 1
                    border.color: machineUserData.activeFocus ? Theme.indigo500 : Theme.border
                }
            }

            FormCheck {
                id: isolatedBox
                text: qsTr("Modo isolado (sem mounts, agent SSH nem rede para o host)")
                enabled: !backend.machineCreateActive
                onToggled: {
                    if (checked) {
                        machineMounts.text = ""
                        forwardSshAgentBox.checked = false
                        isolateNetworkBox.checked = true
                    } else {
                        isolateNetworkBox.checked = false
                    }
                }
            }

            Text {
                text: qsTr("Mounts do host (origem:destino[:ro], um por linha)")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
            }

            TextArea {
                id: machineMounts
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                enabled: !backend.machineCreateActive && !isolatedBox.checked
                placeholderText: "/home/voce/projetos:/mnt/podstack/projetos\n/dados:/dados:ro"
                color: Theme.textPrimary
                placeholderTextColor: Theme.textTertiary
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontMeta
                wrapMode: TextArea.WrapAnywhere
                selectByMouse: true
                selectionColor: Theme.accentSoft
                selectedTextColor: Theme.textPrimary

                background: Rectangle {
                    color: Theme.bgSurface2
                    radius: Theme.radiusSm
                    border.width: 1
                    border.color: machineMounts.activeFocus ? Theme.indigo500 : Theme.border
                }
            }

            FormCheck {
                id: isolateNetworkBox
                text: qsTr("Isolar a rede da machine")
                enabled: !backend.machineCreateActive && !isolatedBox.checked
            }

            FormCheck {
                id: forwardSshAgentBox
                text: qsTr("Encaminhar o SSH agent")
                enabled: !backend.machineCreateActive && !isolatedBox.checked
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: backend.machineCreateActive || backend.machineCreateError.length > 0
                         || backend.machineCreateStage === "cancelled"

                Text {
                    Layout.fillWidth: true
                    text: machinesPage.stageLabel(backend.machineCreateStage)
                    color: backend.machineCreateError.length > 0 ? Theme.danger
                                                                 : Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    elide: Text.ElideRight
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 6
                    radius: 3
                    color: Theme.bgSurface2

                    Rectangle {
                        width: parent.width
                               * Math.max(0, Math.min(1, backend.machineCreateProgress / 100))
                        height: parent.height
                        radius: parent.radius
                        color: Theme.indigo500
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: backend.machineCreateError.length > 0
                    text: backend.machineCreateError
                    color: Theme.danger
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                }
            }
        }

        footer: AppFooter {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: backend.machineCreateActive ? qsTr("Cancelar criação") : qsTr("Fechar")
                variant: "secondary"
                onClicked: {
                    if (backend.machineCreateActive) {
                        backend.cancelMachineCreate()
                    } else {
                        createDialog.close()
                    }
                }
            }
            ActionButton {
                text: qsTr("Criar")
                variant: "primary"
                enabled: !backend.machineCreateActive && machineName.text.length > 0
                         && machinesPage.distroLabels.length > 0
                onClicked: machinesPage.createRequested()
            }
        }
    }

    AppDialog {
        id: deleteMachineDialog
        title: qsTr("Remover machine?")
        modal: true
        width: 440
        anchors.centerIn: parent
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.border
        }

        contentItem: Text {
            text: qsTr("A machine e o disco serão removidos. Esta ação não pode ser desfeita.")
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            wrapMode: Text.WordWrap
        }

        footer: AppFooter {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                onClicked: {
                    machinesPage.pendingDeleteId = ""
                    deleteMachineDialog.close()
                }
            }
            ActionButton {
                text: qsTr("Remover")
                variant: "danger"
                onClicked: {
                    if (machinesPage.pendingDeleteId.length > 0) {
                        backend.removeMachine(machinesPage.pendingDeleteId, true)
                        machinesPage.pendingDeleteId = ""
                    }
                    deleteMachineDialog.close()
                }
            }
        }
    }

    AppDialog {
        id: exportDialog
        title: qsTr("Exportar machine")
        modal: true
        width: 520
        anchors.centerIn: parent
        closePolicy: backend.machineCreateActive ? Popup.NoAutoClose
                                                 : Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: machinesPage.pendingExportId = ""
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.border
        }

        contentItem: ColumnLayout {
            spacing: 8

            Text {
                text: qsTr("Arquivo .tar.zst de destino")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
            }
            PathField {
                id: exportPath
                Layout.fillWidth: true
                title: qsTr("Exportar machine")
                mode: "save"
                filter: qsTr("Machine PodStack (*.tar.zst);;Todos os arquivos (*)")
                placeholderText: qsTr("/home/voce/Downloads/machine.tar.zst")
                enabled: !backend.machineCreateActive
            }

            FormCheck {
                id: exportForce
                text: qsTr("Substituir o arquivo se já existir")
                enabled: !backend.machineCreateActive
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: backend.machineCreateActive || backend.machineCreateError.length > 0
                         || backend.machineCreateStage === "cancelled"
                         || backend.machineCreateStage === "done"

                Text {
                    Layout.fillWidth: true
                    text: machinesPage.archiveStageLabel(backend.machineCreateStage)
                    color: backend.machineCreateError.length > 0 ? Theme.danger
                                                                 : Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    elide: Text.ElideRight
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 6
                    radius: 3
                    color: Theme.bgSurface2

                    Rectangle {
                        width: parent.width
                               * Math.max(0, Math.min(1, backend.machineCreateProgress / 100))
                        height: parent.height
                        radius: parent.radius
                        color: Theme.indigo500
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: backend.machineCreateError.length > 0
                    text: backend.machineCreateError
                    color: Theme.danger
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                }
            }
        }

        footer: AppFooter {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: backend.machineCreateActive ? qsTr("Cancelar export") : qsTr("Fechar")
                variant: "secondary"
                onClicked: {
                    if (backend.machineCreateActive) {
                        backend.cancelMachineCreate()
                    } else {
                        exportDialog.close()
                    }
                }
            }
            ActionButton {
                text: qsTr("Exportar")
                variant: "primary"
                enabled: !backend.machineCreateActive && exportPath.text.length > 0
                         && machinesPage.pendingExportId.length > 0
                         && backend.machineCreateStage !== "done"
                onClicked: backend.exportMachine(machinesPage.pendingExportId,
                                                 exportPath.text, exportForce.checked)
            }
        }
    }

    AppDialog {
        id: importDialog
        title: qsTr("Importar machine")
        modal: true
        width: 520
        anchors.centerIn: parent
        closePolicy: backend.machineCreateActive ? Popup.NoAutoClose
                                                 : Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.border
        }

        contentItem: ColumnLayout {
            spacing: 8

            Text {
                text: qsTr("Arquivo .tar.zst")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
            }
            PathField {
                id: importPath
                Layout.fillWidth: true
                title: qsTr("Importar machine")
                filter: qsTr("Machine PodStack (*.tar.zst);;Todos os arquivos (*)")
                placeholderText: qsTr("/home/voce/Downloads/machine.tar.zst")
                enabled: !backend.machineCreateActive
            }

            Text {
                text: qsTr("Nome da nova machine (opcional)")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
            }
            FormField {
                id: importName
                Layout.fillWidth: true
                placeholderText: qsTr("usado no arquivo se vazio")
                enabled: !backend.machineCreateActive
            }

            FormCheck {
                id: importForce
                text: qsTr("Substituir uma machine existente com o mesmo nome")
                enabled: !backend.machineCreateActive
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: backend.machineCreateActive || backend.machineCreateError.length > 0
                         || backend.machineCreateStage === "cancelled"
                         || backend.machineCreateStage === "done"

                Text {
                    Layout.fillWidth: true
                    text: machinesPage.archiveStageLabel(backend.machineCreateStage)
                    color: backend.machineCreateError.length > 0 ? Theme.danger
                                                                 : Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    elide: Text.ElideRight
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 6
                    radius: 3
                    color: Theme.bgSurface2

                    Rectangle {
                        width: parent.width
                               * Math.max(0, Math.min(1, backend.machineCreateProgress / 100))
                        height: parent.height
                        radius: parent.radius
                        color: Theme.indigo500
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: backend.machineCreateError.length > 0
                    text: backend.machineCreateError
                    color: Theme.danger
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                }
            }
        }

        footer: AppFooter {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: backend.machineCreateActive ? qsTr("Cancelar import") : qsTr("Fechar")
                variant: "secondary"
                onClicked: {
                    if (backend.machineCreateActive) {
                        backend.cancelMachineCreate()
                    } else {
                        importDialog.close()
                    }
                }
            }
            ActionButton {
                text: qsTr("Importar")
                variant: "primary"
                enabled: !backend.machineCreateActive && importPath.text.length > 0
                         && backend.machineCreateStage !== "done"
                onClicked: backend.importMachine(importPath.text, importName.text,
                                                 importForce.checked)
            }
        }
    }

    AppDialog {
        id: renameDialog
        title: qsTr("Renomear machine")
        modal: true
        width: 440
        anchors.centerIn: parent
        onClosed: machinesPage.pendingRenameId = ""
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.border
        }

        contentItem: ColumnLayout {
            spacing: 8

            Text {
                text: qsTr("Novo nome")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
            }
            FormField {
                id: renameName
                Layout.fillWidth: true
                placeholderText: qsTr("novo-nome")
                onAccepted: {
                    if (renameName.text.trim().length > 0
                            && machinesPage.pendingRenameId.length > 0) {
                        backend.renameMachine(machinesPage.pendingRenameId,
                                              renameName.text.trim())
                        renameDialog.close()
                    }
                }
            }
        }

        footer: AppFooter {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                onClicked: renameDialog.close()
            }
            ActionButton {
                text: qsTr("Renomear")
                variant: "primary"
                enabled: renameName.text.trim().length > 0
                         && machinesPage.pendingRenameId.length > 0
                onClicked: {
                    backend.renameMachine(machinesPage.pendingRenameId,
                                          renameName.text.trim())
                    renameDialog.close()
                }
            }
        }
    }

    AppDialog {
        id: cloneDialog
        title: qsTr("Clonar machine")
        modal: true
        width: 520
        anchors.centerIn: parent
        closePolicy: backend.machineCreateActive ? Popup.NoAutoClose
                                                 : Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: machinesPage.pendingCloneId = ""
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.border
        }

        contentItem: ColumnLayout {
            spacing: 8

            Text {
                text: qsTr("Nome da nova machine")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
            }
            FormField {
                id: cloneName
                Layout.fillWidth: true
                placeholderText: qsTr("copia-da-machine")
                enabled: !backend.machineCreateActive
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: backend.machineCreateActive || backend.machineCreateError.length > 0
                         || backend.machineCreateStage === "cancelled"
                         || backend.machineCreateStage === "done"

                Text {
                    Layout.fillWidth: true
                    text: machinesPage.archiveStageLabel(backend.machineCreateStage)
                    color: backend.machineCreateError.length > 0 ? Theme.danger
                                                                 : Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    elide: Text.ElideRight
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 6
                    radius: 3
                    color: Theme.bgSurface2

                    Rectangle {
                        width: parent.width
                               * Math.max(0, Math.min(1, backend.machineCreateProgress / 100))
                        height: parent.height
                        radius: parent.radius
                        color: Theme.indigo500
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: backend.machineCreateError.length > 0
                    text: backend.machineCreateError
                    color: Theme.danger
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                }
            }
        }

        footer: AppFooter {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: backend.machineCreateActive ? qsTr("Cancelar clone") : qsTr("Fechar")
                variant: "secondary"
                onClicked: {
                    if (backend.machineCreateActive) {
                        backend.cancelMachineCreate()
                    } else {
                        cloneDialog.close()
                    }
                }
            }
            ActionButton {
                text: qsTr("Clonar")
                variant: "primary"
                enabled: !backend.machineCreateActive && cloneName.text.trim().length > 0
                         && machinesPage.pendingCloneId.length > 0
                         && backend.machineCreateStage !== "done"
                onClicked: backend.cloneMachine(machinesPage.pendingCloneId,
                                                cloneName.text.trim())
            }
        }
    }

    AppDialog {
        id: serialDialog
        title: qsTr("Portas seriais")
        modal: true
        width: 560
        anchors.centerIn: parent
        closePolicy: machinesPage.serialActionPending ? Popup.NoAutoClose
                                                       : Popup.CloseOnEscape
                                                         | Popup.CloseOnPressOutside
        onClosed: {
            machinesPage.pendingSerialId = ""
            machinesPage.serialActionPending = false
            serialError.text = ""
        }

        contentItem: ColumnLayout {
            spacing: 9

            Text {
                Layout.fillWidth: true
                text: qsTr("Anexe dispositivos seriais do host (ttyUSB/ttyACM/UART). "
                           + "A máquina reinicia ao anexar ou desanexar se estiver rodando.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            Text {
                text: qsTr("Anexadas")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                visible: (backend.machineInfo.serialPorts || []).length === 0
                text: qsTr("Nenhuma porta anexada.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
            }

            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(160, Math.max(1,
                    (backend.machineInfo.serialPorts || []).length) * 36)
                clip: true
                model: backend.machineInfo.serialPorts || []
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width
                    height: 36
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        text: String(modelData)
                        color: Theme.textPrimary
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontMeta
                        elide: Text.ElideMiddle
                    }
                    ActionButton {
                        text: qsTr("Desanexar")
                        size: "sm"
                        variant: "danger"
                        enabled: !machinesPage.serialActionPending
                        onClicked: machinesPage.detachSerialPort(String(modelData))
                    }
                }
            }

            Text {
                text: qsTr("Disponíveis no host")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                visible: backend.machineSerialPorts.length === 0
                text: qsTr("Nenhuma porta serial disponível no host.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
            }

            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(220, Math.max(1,
                    backend.machineSerialPorts.length) * 36)
                clip: true
                model: backend.machineSerialPorts
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width
                    height: 36
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        text: String(modelData.path) + "  " + String(modelData.description || "")
                        color: Theme.textPrimary
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontMeta
                        elide: Text.ElideMiddle
                    }
                    ActionButton {
                        text: qsTr("Anexar")
                        size: "sm"
                        enabled: !machinesPage.serialActionPending
                                 && modelData.readable === true
                                 && !(backend.machineInfo.serialPorts || []).includes(
                                     String(modelData.path))
                        onClicked: machinesPage.attachSerialPort(String(modelData.path))
                    }
                }
            }

            Text {
                id: serialError
                Layout.fillWidth: true
                visible: text.length > 0
                color: Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton {
                    text: qsTr("Fechar")
                    onClicked: serialDialog.close()
                }
            }
        }
    }

    AppDialog {
        id: portsDialog
        title: qsTr("Portas da machine")
        modal: true
        width: 560
        anchors.centerIn: parent
        closePolicy: machinesPage.portsActionPending ? Popup.NoAutoClose
                                                     : Popup.CloseOnEscape
                                                       | Popup.CloseOnPressOutside
        onClosed: {
            machinesPage.pendingPortsId = ""
            machinesPage.portsActionPending = false
            machinesPage.portsDraft = []
            portsError.text = ""
        }
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.border
        }

        contentItem: ColumnLayout {
            spacing: 9

            Text {
                Layout.fillWidth: true
                text: qsTr("A porta SSH (22/tcp) é gerenciada automaticamente e não aparece na lista.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(170, machinesPage.portsDraft.length * 36 + 8)
                visible: machinesPage.portsDraft.length > 0
                radius: Theme.radiusSm
                color: Theme.bgSurface2
                border.width: 1
                border.color: Theme.border

                ListView {
                    id: portsDraftList
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    model: machinesPage.portsDraft
                    spacing: 2

                    delegate: RowLayout {
                        required property var modelData
                        required property int index

                        width: portsDraftList.width
                        spacing: 8

                        Text {
                            Layout.preferredWidth: 76
                            text: modelData.hostPort + " → " + modelData.guestPort
                            color: Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontMicro
                        }

                        Text {
                            Layout.preferredWidth: 40
                            text: modelData.protocol
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMicro
                        }

                        Text {
                            Layout.fillWidth: true
                            text: modelData.exposeLan ? qsTr("0.0.0.0 (LAN)")
                                                      : qsTr("127.0.0.1")
                            color: modelData.exposeLan ? Theme.warning : Theme.textTertiary
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontMicro
                        }

                        Text {
                            Layout.preferredWidth: 110
                            text: modelData.label
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMicro
                            elide: Text.ElideRight
                        }

                        ActionButton {
                            text: qsTr("Remover")
                            size: "sm"
                            variant: "ghost"
                            enabled: !machinesPage.portsActionPending
                            onClicked: machinesPage.removePortDraft(index)
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: machinesPage.portsDraft.length === 0
                text: qsTr("Nenhuma porta extra configurada.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 7

                Text { text: qsTr("Porta do host"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: portsHost
                    Layout.fillWidth: true
                    enabled: !machinesPage.portsActionPending
                    validator: IntValidator { bottom: 1; top: 65535 }
                }

                Text { text: qsTr("Porta do guest"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: portsGuest
                    Layout.fillWidth: true
                    enabled: !machinesPage.portsActionPending
                    validator: IntValidator { bottom: 1; top: 65535 }
                }

                Text { text: qsTr("Protocolo"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                AppSelect {
                    id: portsProtocol
                    Layout.fillWidth: true
                    model: ["tcp", "udp"]
                    currentIndex: 0
                    enabled: !machinesPage.portsActionPending
                }

                Text { text: qsTr("Rótulo"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: portsLabel
                    Layout.fillWidth: true
                    placeholderText: qsTr("opcional (até 64 caracteres)")
                    enabled: !machinesPage.portsActionPending
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                FormCheck {
                    id: portsLan
                    text: qsTr("Expor na LAN (0.0.0.0)")
                    enabled: !machinesPage.portsActionPending
                }

                Item { Layout.fillWidth: true }

                ActionButton {
                    text: qsTr("Adicionar")
                    size: "sm"
                    variant: "secondary"
                    enabled: !machinesPage.portsActionPending
                    onClicked: machinesPage.addPortDraft()
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("Aviso: expor na LAN faz o QEMU escutar em 0.0.0.0 e permite que outros dispositivos acessem a porta; o firewall do host também precisa liberá-la.")
                color: Theme.warning
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            Text {
                id: portsError
                Layout.fillWidth: true
                visible: text.length > 0
                text: ""
                color: Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }

        footer: AppFooter {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                enabled: !machinesPage.portsActionPending
                onClicked: portsDialog.close()
            }
            ActionButton {
                text: qsTr("Aplicar")
                variant: "primary"
                enabled: !machinesPage.portsActionPending
                         && machinesPage.pendingPortsId.length > 0
                onClicked: {
                    portsError.text = ""
                    machinesPage.portsActionPending = true
                    backend.setMachinePorts(machinesPage.pendingPortsId,
                                            machinesPage.portsDraft)
                }
            }
        }
    }

    AppDialog {
        id: resourcesDialog
        title: qsTr("Editar recursos")
        modal: true
        width: 460
        anchors.centerIn: parent
        closePolicy: machinesPage.resourcesActionPending ? Popup.NoAutoClose
                                                         : Popup.CloseOnEscape
                                                           | Popup.CloseOnPressOutside
        onClosed: {
            machinesPage.pendingResourcesId = ""
            machinesPage.resourcesActionPending = false
        }
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.border
        }

        contentItem: ColumnLayout {
            spacing: 9

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 7

                Text { text: qsTr("CPUs"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: resourcesCpus
                    Layout.fillWidth: true
                    enabled: !machinesPage.resourcesActionPending
                    validator: IntValidator { bottom: 1; top: 256 }
                }

                Text { text: qsTr("Memória (GiB)"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: resourcesMemory
                    Layout.fillWidth: true
                    enabled: !machinesPage.resourcesActionPending
                    validator: IntValidator { bottom: 1; top: 1024 }
                }

                Text { text: qsTr("Disco (GiB)"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: resourcesDisk
                    Layout.fillWidth: true
                    enabled: !machinesPage.resourcesActionPending
                    validator: IntValidator { bottom: 1; top: 4096 }
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("O disco só pode crescer; alterações de CPU/memória exigem reiniciar a machine.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            Text {
                id: resourcesError
                Layout.fillWidth: true
                visible: text.length > 0
                text: ""
                color: Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }

        footer: AppFooter {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                enabled: !machinesPage.resourcesActionPending
                onClicked: resourcesDialog.close()
            }
            ActionButton {
                text: qsTr("Aplicar")
                variant: "primary"
                enabled: !machinesPage.resourcesActionPending
                         && machinesPage.pendingResourcesId.length > 0
                onClicked: {
                    const cpus = parseInt(resourcesCpus.text)
                    const memoryGiB = parseInt(resourcesMemory.text)
                    const diskGiB = parseInt(resourcesDisk.text)
                    if (isNaN(cpus) || isNaN(memoryGiB) || isNaN(diskGiB)) {
                        resourcesError.text = qsTr("Preencha CPUs, memória e disco.")
                        return
                    }
                    const spec = { "id": machinesPage.pendingResourcesId }
                    if (cpus !== machinesPage.resourcesOriginalCpus) {
                        spec.cpus = cpus
                    }
                    if (memoryGiB !== machinesPage.resourcesOriginalMemoryGiB) {
                        spec.memoryGiB = memoryGiB
                    }
                    if (diskGiB !== machinesPage.resourcesOriginalDiskGiB) {
                        spec.diskGiB = diskGiB
                    }
                    if (Object.keys(spec).length === 1) {
                        resourcesError.text = qsTr("Nenhuma alteração para aplicar.")
                        return
                    }
                    resourcesError.text = ""
                    machinesPage.resourcesActionPending = true
                    backend.updateMachine(spec)
                }
            }
        }
    }

    AppDialog {
        id: snapshotsDialog
        title: qsTr("Snapshots do disco")
        modal: true
        width: 620
        anchors.centerIn: parent
        closePolicy: machinesPage.snapshotActionPending ? Popup.NoAutoClose
                                                        : Popup.CloseOnEscape
                                                          | Popup.CloseOnPressOutside
        onClosed: {
            machinesPage.pendingSnapshotsId = ""
            machinesPage.snapshotActionPending = false
            machinesPage.pendingSnapshotAction = ""
            machinesPage.pendingSnapshotName = ""
            snapshotsError.text = ""
        }
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.border
        }

        contentItem: ColumnLayout {
            spacing: 9

            Text {
                Layout.fillWidth: true
                text: qsTr("Snapshots são internos do qcow2. Restaurar substitui o disco atual; exportar ou clonar a machine não os preserva.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(220, Math.max(44,
                    backend.machineSnapshots.length * 38 + 8))
                visible: backend.machineSnapshots.length > 0
                radius: Theme.radiusSm
                color: Theme.bgSurface2
                border.width: 1
                border.color: Theme.border

                ListView {
                    id: snapshotsList
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    model: backend.machineSnapshots
                    spacing: 2

                    delegate: Item {
                        required property var modelData

                        width: snapshotsList.width
                        height: 38

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 10

                            Text {
                                Layout.preferredWidth: 140
                                Layout.alignment: Qt.AlignVCenter
                                text: modelData.name
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMeta
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }

                            Text {
                                Layout.preferredWidth: 170
                                Layout.alignment: Qt.AlignVCenter
                                text: {
                                    const date = new Date(modelData.date)
                                    return isNaN(date.getTime())
                                           ? String(modelData.date || "-")
                                           : Qt.formatDateTime(date, "yyyy-MM-dd HH:mm:ss")
                                }
                                color: Theme.textTertiary
                                font.family: Theme.monoFamily
                                font.pixelSize: Theme.fontMicro
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                Layout.preferredWidth: 60
                                Layout.alignment: Qt.AlignVCenter
                                text: machinesPage.snapshotSize(modelData.sizeBytes)
                                color: Theme.textSecondary
                                font.family: Theme.monoFamily
                                font.pixelSize: Theme.fontMicro
                                horizontalAlignment: Text.AlignRight
                                verticalAlignment: Text.AlignVCenter
                            }

                            ActionButton {
                                text: qsTr("Restaurar")
                                size: "sm"
                                variant: "ghost"
                                Layout.alignment: Qt.AlignVCenter
                                enabled: !machinesPage.snapshotActionPending
                                         && !backend.actionActive
                                onClicked: machinesPage.requestSnapshotAction("restore",
                                                                              String(modelData.name))
                            }

                            ActionButton {
                                text: qsTr("Remover")
                                size: "sm"
                                variant: "danger"
                                Layout.alignment: Qt.AlignVCenter
                                enabled: !machinesPage.snapshotActionPending
                                         && !backend.actionActive
                                onClicked: machinesPage.requestSnapshotAction("remove",
                                                                              String(modelData.name))
                            }
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: backend.machineSnapshots.length === 0
                text: qsTr("Nenhum snapshot criado.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
            }

            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: backend.machineSnapshotsError
                color: Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: confirmRow.implicitHeight + 16
                visible: machinesPage.pendingSnapshotAction.length > 0
                radius: Theme.radiusSm
                color: machinesPage.pendingSnapshotAction === "remove"
                       ? Theme.dangerSoft : Theme.warningSoft
                border.width: 1
                border.color: machinesPage.pendingSnapshotAction === "remove"
                              ? Theme.danger : Theme.warning

                RowLayout {
                    id: confirmRow
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        text: machinesPage.pendingSnapshotAction === "remove"
                              ? qsTr("Remover o snapshot %1? Esta ação não pode ser desfeita.")
                                    .arg(machinesPage.pendingSnapshotName)
                              : qsTr("Restaurar o snapshot %1? O disco atual será substituído e as alterações não salvas serão perdidas.")
                                    .arg(machinesPage.pendingSnapshotName)
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMeta
                        wrapMode: Text.WordWrap
                    }

                    ActionButton {
                        text: qsTr("Cancelar")
                        size: "sm"
                        variant: "secondary"
                        Layout.alignment: Qt.AlignVCenter
                        enabled: !machinesPage.snapshotActionPending
                        onClicked: {
                            machinesPage.pendingSnapshotAction = ""
                            machinesPage.pendingSnapshotName = ""
                        }
                    }

                    ActionButton {
                        text: machinesPage.pendingSnapshotAction === "remove"
                              ? qsTr("Remover") : qsTr("Restaurar")
                        size: "sm"
                        variant: machinesPage.pendingSnapshotAction === "remove"
                                 ? "danger" : "primary"
                        Layout.alignment: Qt.AlignVCenter
                        enabled: !machinesPage.snapshotActionPending
                        onClicked: machinesPage.confirmSnapshotAction()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                FormField {
                    id: snapshotsName
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    placeholderText: qsTr("[A-Za-z0-9._-], até 64 caracteres")
                    enabled: !machinesPage.snapshotActionPending
                }

                ActionButton {
                    text: qsTr("Criar")
                    size: "sm"
                    variant: "primary"
                    Layout.alignment: Qt.AlignVCenter
                    enabled: !machinesPage.snapshotActionPending
                             && !backend.actionActive
                             && machinesPage.pendingSnapshotsId.length > 0
                             && snapshotsName.text.trim().length > 0
                    onClicked: machinesPage.createSnapshot()
                }
            }

            Text {
                id: snapshotsError
                Layout.fillWidth: true
                visible: text.length > 0
                text: ""
                color: Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }

        footer: AppFooter {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Fechar")
                variant: "secondary"
                enabled: !machinesPage.snapshotActionPending
                onClicked: snapshotsDialog.close()
            }
        }
    }

    Component.onCompleted: {
        backend.refreshMachines()
        backend.refreshMachineDistros()
        syncSelection()
    }
}
