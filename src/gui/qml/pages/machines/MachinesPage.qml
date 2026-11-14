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
            onClicked: backend.machines.batch(machineSelection.ids(), "delete", true)
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
                renameName.text = ""
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
        target: backend

        function onMachinesChanged() {
            machinesPage.syncSelection()
        }

        function onMachineCreateStateChanged() {
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

    AppModal {
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
            AppButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                onClicked: {
                    machinesPage.pendingDeleteId = ""
                    deleteMachineDialog.close()
                }
            }
            AppButton {
                text: qsTr("Remover")
                variant: "danger"
                onClicked: {
                    if (machinesPage.pendingDeleteId.length > 0) {
                        backend.machines.remove(machinesPage.pendingDeleteId, true)
                        machinesPage.pendingDeleteId = ""
                    }
                    deleteMachineDialog.close()
                }
            }
        }
    }

    AppModal {
        id: importDialog
        title: qsTr("Importar machine")
        modal: true
        width: 520
        anchors.centerIn: parent
        closePolicy: backend.machines.createActive ? Popup.NoAutoClose
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
            AppPathField {
                id: importPath
                Layout.fillWidth: true
                title: qsTr("Importar machine")
                filter: qsTr("Machine PodStack (*.tar.zst);;Todos os arquivos (*)")
                placeholderText: qsTr("/home/voce/Downloads/machine.tar.zst")
                enabled: !backend.machines.createActive
            }

            Text {
                text: qsTr("Nome da nova machine (opcional)")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
            }
            AppInput {
                id: importName
                Layout.fillWidth: true
                placeholderText: qsTr("usado no arquivo se vazio")
                enabled: !backend.machines.createActive
            }

            AppCheck {
                id: importForce
                text: qsTr("Substituir uma machine existente com o mesmo nome")
                enabled: !backend.machines.createActive
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: backend.machines.createActive || backend.machines.createError.length > 0
                         || backend.machines.createStage === "cancelled"
                         || backend.machines.createStage === "done"

                Text {
                    Layout.fillWidth: true
                    text: machinesPage.archiveStageLabel(backend.machines.createStage)
                    color: backend.machines.createError.length > 0 ? Theme.danger
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
                               * Math.max(0, Math.min(1, backend.machines.createProgress / 100))
                        height: parent.height
                        radius: parent.radius
                        color: Theme.indigo500
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: backend.machines.createError.length > 0
                    text: backend.machines.createError
                    color: Theme.danger
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                }
            }
        }

        footer: AppFooter {
            Item { Layout.fillWidth: true }
            AppButton {
                text: backend.machines.createActive ? qsTr("Cancelar import") : qsTr("Fechar")
                variant: "secondary"
                onClicked: {
                    if (backend.machines.createActive) {
                        backend.machines.cancelCreate()
                    } else {
                        importDialog.close()
                    }
                }
            }
            AppButton {
                text: qsTr("Importar")
                variant: "primary"
                enabled: !backend.machines.createActive && importPath.text.length > 0
                         && backend.machines.createStage !== "done"
                onClicked: backend.machines.importFrom(importPath.text, importName.text,
                                                 importForce.checked)
            }
        }
    }

    AppModal {
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
            AppInput {
                id: renameName
                Layout.fillWidth: true
                placeholderText: qsTr("novo-nome")
                onAccepted: {
                    if (renameName.text.trim().length > 0
                            && machinesPage.pendingRenameId.length > 0) {
                        backend.machines.rename(machinesPage.pendingRenameId,
                                              renameName.text.trim())
                        renameDialog.close()
                    }
                }
            }
        }

        footer: AppFooter {
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                onClicked: renameDialog.close()
            }
            AppButton {
                text: qsTr("Renomear")
                variant: "primary"
                enabled: renameName.text.trim().length > 0
                         && machinesPage.pendingRenameId.length > 0
                onClicked: {
                    backend.machines.rename(machinesPage.pendingRenameId,
                                          renameName.text.trim())
                    renameDialog.close()
                }
            }
        }
    }

    Component.onCompleted: {
        backend.machines.refresh()
        backend.machines.refreshDistros()
        syncSelection()
    }
}
