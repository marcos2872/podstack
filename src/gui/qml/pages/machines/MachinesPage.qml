import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Página de machines: catálogo, inspetor e diálogos de ciclo de vida.
// Cada diálogo vive no próprio componente; a página mantém a seleção, os
// defaults de criação e os diálogos simples (importar/renomear/remover).
ColumnLayout {
    id: machinesPage

    property string selectedMachineId: ""
    property string pendingDeleteId: ""
    property string pendingRenameId: ""

    signal machineExpandRequested(string id, int tab)

    readonly property var settingsValues: backend.settings.values
    // Defaults de criação vindos de Configurações (machines.*); caem no valor
    // histórico se a chave ainda não carregou.
    readonly property int defaultCpus: {
        const value = Number(settingsValues["machines.cpus"])
        return isFinite(value) && value >= 1 ? value : 2
    }
    readonly property int defaultMemoryGiB: {
        const bytes = Number(settingsValues["machines.memoryBytes"])
        return isFinite(bytes) && bytes >= 1073741824 ? Math.round(bytes / 1073741824) : 4
    }
    readonly property int defaultDiskGiB: {
        const bytes = Number(settingsValues["machines.diskBytes"])
        return isFinite(bytes) && bytes >= 1073741824 ? Math.round(bytes / 1073741824) : 20
    }
    readonly property bool defaultIsolated: settingsValues["machines.isolated"] === true
    readonly property bool defaultForwardSshAgent:
        settingsValues["machines.forwardSshAgent"] === true

    readonly property int machineCount: backend.machines.items.length
    readonly property int runningCount: {
        let count = 0
        for (let i = 0; i < backend.machines.items.length; ++i) {
            if (backend.machines.items[i].state === "running") {
                ++count
            }
        }
        return count
    }

    readonly property var selectedMachine: {
        if (selectedMachineId.length === 0) {
            return ({})
        }
        const info = backend.machines.info
        let listItem = null
        for (let i = 0; i < backend.machines.items.length; ++i) {
            if (String(backend.machines.items[i].id) === selectedMachineId) {
                listItem = backend.machines.items[i]
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
        for (let i = 0; i < backend.machines.distros.length; ++i) {
            const distro = backend.machines.distros[i]
            labels.push(distro.label || distro.distro || "")
        }
        return labels
    }

    function selectMachine(id) {
        selectedMachineId = id
        if (id.length > 0) {
            backend.machines.loadInfo(id)
            backend.machineDetail.loadLogs(id, 200)
        }
    }

    function syncSelection() {
        const items = backend.machines.items
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
        machineCatalog.currentRow = index
        if (index >= 0) {
            backend.machines.loadInfo(selectedMachineId)
            backend.machineDetail.loadLogs(selectedMachineId, 200)
        }
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

    spacing: 0

    SelectionState { id: machineSelection }

    AppViewHeader {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.topMargin: 20
        Layout.bottomMargin: 16
        title: qsTr("Machines")
        subtitle: machinesPage.runningCount + qsTr(" de ") + machinesPage.machineCount
                  + qsTr(" em execução")

        AppButton {
            text: qsTr("Nova machine")
            variant: "primary"
            iconName: "plus"
            enabled: !backend.machines.createActive
            onClicked: createDialog.openDialog()
        }

        AppButton {
            text: qsTr("Importar")
            variant: "ghost"
            iconName: "download"
            enabled: !backend.machines.createActive
            onClicked: {
                importPath.text = ""
                importName.text = ""
                importForce.checked = false
                backend.machines.resetCreateState()
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

        AppButton {
            visible: machineSelection.count > 0
            text: qsTr("Iniciar")
            iconName: "play"
            onClicked: backend.machines.batch(machineSelection.ids(), "start")
        }

        AppButton {
            visible: machineSelection.count > 0
            text: qsTr("Parar")
            iconName: "stop"
            onClicked: backend.machines.batch(machineSelection.ids(), "stop")
        }

        AppButton {
            visible: machineSelection.count > 0
            text: qsTr("Remover")
            variant: "danger"
            iconName: "trash"
            onClicked: batchRemoveDialog.open()
        }

        AppButton {
            variant: "ghost"
            iconName: "refresh"
            onClicked: backend.machines.refresh()
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

        MachineCatalog {
            id: machineCatalog

            items: backend.machines.items
            selection: machineSelection

            onActivated: function(index) {
                if (index >= 0 && index < backend.machines.items.length) {
                    machinesPage.selectMachine(String(backend.machines.items[index].id))
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
                exportDialog.openFor(id)
            }
            onRenameRequested: function(id) {
                machinesPage.pendingRenameId = id
                renameDialog.open()
            }
            onCloneRequested: function(id) {
                cloneDialog.openFor(id)
            }
            onPortsRequested: function(id) {
                portsDialog.openFor(id, machinesPage.selectedMachine)
            }
            onSerialRequested: function(id) {
                serialDialog.openFor(id)
            }
            onUsbRequested: function(id) {
                usbDialog.openFor(id)
            }
            onSnapshotsRequested: function(id) {
                snapshotsDialog.openFor(id)
            }
            onResourcesRequested: function(id) {
                resourcesDialog.openFor(id, machinesPage.selectedMachine)
            }
        }
    }

    Connections {
        target: backend.machines

        function onItemsChanged() {
            machinesPage.syncSelection()
        }

        function onCreateStateChanged() {
            if (backend.machines.createActive || backend.machines.createStage !== "done") {
                return
            }
            if (backend.machines.jobKind === "import") {
                importDialog.close()
            }
        }
    }

    MachineCreateDialog {
        id: createDialog
        distroLabels: machinesPage.distroLabels
        defaultCpus: machinesPage.defaultCpus
        defaultMemoryGiB: machinesPage.defaultMemoryGiB
        defaultDiskGiB: machinesPage.defaultDiskGiB
        defaultIsolated: machinesPage.defaultIsolated
        defaultForwardSshAgent: machinesPage.defaultForwardSshAgent
        stageLabel: machinesPage.stageLabel
    }

    MachineExportDialog {
        id: exportDialog
        archiveStageLabel: machinesPage.archiveStageLabel
    }

    MachineCloneDialog {
        id: cloneDialog
        archiveStageLabel: machinesPage.archiveStageLabel
    }

    MachinePortsDialog {
        id: portsDialog
        selectedMachine: machinesPage.selectedMachine
    }

    MachineSerialDialog {
        id: serialDialog
    }

    MachineUsbDialog {
        id: usbDialog
    }

    MachineResourcesDialog {
        id: resourcesDialog
        selectedMachine: machinesPage.selectedMachine
    }

    MachineSnapshotsDialog {
        id: snapshotsDialog
    }

    MachineDeleteDialog {
        id: deleteMachineDialog
        machineId: machinesPage.pendingDeleteId
        onClosed: machinesPage.pendingDeleteId = ""
    }

    MachineImportDialog {
        id: importDialog
        stageLabel: machinesPage.archiveStageLabel
    }

    MachineRenameDialog {
        id: renameDialog
        machineId: machinesPage.pendingRenameId
        onClosed: machinesPage.pendingRenameId = ""
    }

    Component.onCompleted: {
        backend.machines.refresh()
        backend.machines.refreshDistros()
        syncSelection()
    }

    AppConfirmDialog {
        id: batchRemoveDialog
        title: qsTr("Remover machines selecionadas")
        message: qsTr("%1 machine(s) serão removidas com os discos. As que estiverem rodando serão encerradas à força.")
                     .arg(machineSelection.count)
        confirmText: qsTr("Remover")
        destructive: true
        busyText: qsTr("Removendo…")
        actionController: backend.machines
        actionMethod: "machines.batch"
        onConfirmed: backend.machines.batch(machineSelection.ids(), "delete", true)
    }
}
