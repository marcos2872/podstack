import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: machinesPage

    property string selectedMachineId: ""
    property string pendingDeleteId: ""

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
        if (info && String(info.id || "") === selectedMachineId) {
            return info
        }
        for (let i = 0; i < backend.machines.length; ++i) {
            if (String(backend.machines[i].id) === selectedMachineId) {
                return backend.machines[i]
            }
        }
        return ({})
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
        machineList.currentIndex = index
        if (index >= 0) {
            backend.loadMachineInfo(selectedMachineId)
            backend.loadMachineLogs(selectedMachineId, 200)
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

    function createRequested() {
        const index = distroSelect.currentIndex
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
            userData: machineUserData.text,
            isolateNetwork: isolateNetworkBox.checked,
            forwardSshAgent: forwardSshAgentBox.checked
        })
    }

    spacing: 0

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
            onClicked: {
                machineName.text = ""
                machineCpus.text = "2"
                machineMemory.text = "4"
                machineDisk.text = "20"
                machineUser.text = ""
                machineUserData.text = ""
                isolateNetworkBox.checked = false
                forwardSshAgentBox.checked = false
                distroSelect.currentIndex = 0
                backend.refreshMachineDistros()
                createDialog.open()
            }
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
            onDeleteRequested: function(id) {
                machinesPage.pendingDeleteId = id
                deleteMachineDialog.open()
            }
        }
    }

    Connections {
        target: backend

        function onMachinesChanged() {
            machinesPage.syncSelection()
        }

        function onMachineCreateStateChanged() {
            if (!backend.machineCreateActive && backend.machineCreateStage === "done") {
                createDialog.close()
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
                id: isolateNetworkBox
                text: qsTr("Isolar a rede da machine")
                enabled: !backend.machineCreateActive
            }

            FormCheck {
                id: forwardSshAgentBox
                text: qsTr("Encaminhar o SSH agent")
                enabled: !backend.machineCreateActive
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
        standardButtons: Dialog.Yes | Dialog.No

        contentItem: Text {
            text: qsTr("A machine e o disco serão removidos. Esta ação não pode ser desfeita.")
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            wrapMode: Text.WordWrap
        }

        onAccepted: {
            if (machinesPage.pendingDeleteId.length > 0) {
                backend.removeMachine(machinesPage.pendingDeleteId, true)
                machinesPage.pendingDeleteId = ""
            }
        }
        onRejected: machinesPage.pendingDeleteId = ""
    }

    Component.onCompleted: {
        backend.refreshMachines()
        backend.refreshMachineDistros()
        syncSelection()
    }
}
