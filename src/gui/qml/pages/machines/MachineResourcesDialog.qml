import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Diálogo de edição de recursos (CPU, memória e disco) da machine.
AppModal {
    id: root

    property var selectedMachine: ({})
    property string pendingId: ""
    property bool actionPending: false
    property int originalCpus: 0
    property int originalMemoryGiB: 0
    property int originalDiskGiB: 0

    function openFor(id, machine) {
        root.pendingId = id
        root.actionPending = false
        let current = machine || root.selectedMachine
        for (let i = 0; i < backend.machines.items.length; ++i) {
            if (String(backend.machines.items[i].id) === id) {
                current = backend.machines.items[i]
                break
            }
        }
        const gib = 1073741824
        root.originalCpus = Number(current.cpus || 0)
        root.originalMemoryGiB = Math.max(1, Math.round(
            Number(current.memoryBytes || 0) / gib))
        root.originalDiskGiB = Math.max(1, Math.round(
            Number(current.diskBytes || 0) / gib))
        resourcesCpus.text = String(root.originalCpus || 2)
        resourcesMemory.text = String(root.originalMemoryGiB)
        resourcesDisk.text = String(root.originalDiskGiB)
        resourcesError.text = ""
        root.open()
    }

    title: qsTr("Editar recursos")
    modal: true
    width: 460
    anchors.centerIn: parent
    closePolicy: root.actionPending ? Popup.NoAutoClose
                                    : Popup.CloseOnEscape
                                      | Popup.CloseOnPressOutside
    onClosed: {
        root.pendingId = ""
        root.actionPending = false
    }
    background: Rectangle {
        color: Theme.bgSurface
        radius: Theme.radiusLg
        border.width: 1
        border.color: Theme.border
    }

    Connections {
        target: backend

        function onActionFinished(ok, message) {
            if (!root.actionPending || !root.opened) {
                return
            }
            root.actionPending = false
            if (ok) {
                root.close()
            } else {
                resourcesError.text = message.length > 0 ? message : backend.lastError
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 9

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 7

            Text { text: qsTr("CPUs"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
            AppInput {
                id: resourcesCpus
                Layout.fillWidth: true
                enabled: !root.actionPending
                validator: IntValidator { bottom: 1; top: 256 }
            }

            Text { text: qsTr("Memória (GiB)"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
            AppInput {
                id: resourcesMemory
                Layout.fillWidth: true
                enabled: !root.actionPending
                validator: IntValidator { bottom: 1; top: 1024 }
            }

            Text { text: qsTr("Disco (GiB)"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
            AppInput {
                id: resourcesDisk
                Layout.fillWidth: true
                enabled: !root.actionPending
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
        AppButton {
            text: qsTr("Cancelar")
            variant: "secondary"
            enabled: !root.actionPending
            onClicked: root.close()
        }
        AppButton {
            text: qsTr("Aplicar")
            variant: "primary"
            enabled: !root.actionPending && root.pendingId.length > 0
            onClicked: {
                const cpus = parseInt(resourcesCpus.text)
                const memoryGiB = parseInt(resourcesMemory.text)
                const diskGiB = parseInt(resourcesDisk.text)
                if (isNaN(cpus) || isNaN(memoryGiB) || isNaN(diskGiB)) {
                    resourcesError.text = qsTr("Preencha CPUs, memória e disco.")
                    return
                }
                const spec = { "id": root.pendingId }
                if (cpus !== root.originalCpus) {
                    spec.cpus = cpus
                }
                if (memoryGiB !== root.originalMemoryGiB) {
                    spec.memoryGiB = memoryGiB
                }
                if (diskGiB !== root.originalDiskGiB) {
                    spec.diskGiB = diskGiB
                }
                if (Object.keys(spec).length === 1) {
                    resourcesError.text = qsTr("Nenhuma alteração para aplicar.")
                    return
                }
                resourcesError.text = ""
                root.actionPending = true
                backend.machines.update(spec)
            }
        }
    }
}
