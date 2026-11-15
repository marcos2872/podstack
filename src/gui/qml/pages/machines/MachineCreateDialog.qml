import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Diálogo de criação de machine: distro, recursos, cloud-init, mounts e
// opções de isolamento. Os defaults vêm das configurações (machines.*) via
// propriedades calculadas pelo hospedeiro.
AppModal {
    id: root

    property var distroLabels: []
    property int defaultCpus: 2
    property int defaultMemoryGiB: 4
    property int defaultDiskGiB: 20
    property bool defaultIsolated: false
    property bool defaultForwardSshAgent: false
    /// Função do hospedeiro que traduz o estágio de criação em texto.
    property var stageLabel: function(stage) { return stage }

    function openDialog() {
        machineName.text = ""
        machineCpus.text = String(root.defaultCpus)
        machineMemory.text = String(root.defaultMemoryGiB)
        machineDisk.text = String(root.defaultDiskGiB)
        machineUser.text = ""
        machinePassword.text = ""
        machineUserData.text = ""
        machineMounts.text = ""
        isolatedBox.checked = root.defaultIsolated
        isolateNetworkBox.checked = root.defaultIsolated
        forwardSshAgentBox.checked = !root.defaultIsolated
                                    && root.defaultForwardSshAgent
        distroSelect.currentIndex = 0
        backend.machines.resetCreateState()
        backend.machines.refreshDistros()
        root.open()
    }

    function createRequested() {
        const index = distroSelect.currentIndex
        const distro = index >= 0 && index < backend.machines.distros.length
                       ? backend.machines.distros[index] : ({})
        backend.machines.create({
            name: machineName.text,
            distro: distro.distro || "",
            version: distro.version || "",
            cpus: Number(machineCpus.text || root.defaultCpus),
            memoryGiB: Number(machineMemory.text || root.defaultMemoryGiB),
            diskGiB: Number(machineDisk.text || root.defaultDiskGiB),
            user: machineUser.text,
            password: machinePassword.text,
            userData: machineUserData.text,
            mountsText: machineMounts.text,
            isolated: isolatedBox.checked,
            isolateNetwork: isolateNetworkBox.checked,
            forwardSshAgent: forwardSshAgentBox.checked
        })
    }

    title: qsTr("Nova machine")
    modal: true
    width: 580
    closePolicy: backend.machines.createActive
                 ? Popup.NoAutoClose
                 : Popup.CloseOnEscape | Popup.CloseOnPressOutside

    Connections {
        target: backend.machines

        function onCreateStateChanged() {
            if (backend.machines.createActive || backend.machines.createStage !== "done") {
                return
            }
            if (backend.machines.jobKind === "create") {
                root.close()
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

            Text { text: qsTr("Nome"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
            AppInput {
                id: machineName
                Layout.fillWidth: true
                placeholderText: "dev"
                enabled: !backend.machines.createActive
            }

            Text { text: qsTr("Distro"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
            AppSelect {
                id: distroSelect
                Layout.fillWidth: true
                model: root.distroLabels
                enabled: !backend.machines.createActive
                currentIndex: model.length > 0 ? 0 : -1
            }

            Text { text: qsTr("CPUs"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
            AppInput {
                id: machineCpus
                Layout.fillWidth: true
                text: "2"
                enabled: !backend.machines.createActive
                validator: IntValidator { bottom: 1; top: 256 }
            }

            Text { text: qsTr("Memória (GiB)"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
            AppInput {
                id: machineMemory
                Layout.fillWidth: true
                text: "4"
                enabled: !backend.machines.createActive
                validator: IntValidator { bottom: 1; top: 1024 }
            }

            Text { text: qsTr("Disco (GiB)"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
            AppInput {
                id: machineDisk
                Layout.fillWidth: true
                text: "20"
                enabled: !backend.machines.createActive
                validator: IntValidator { bottom: 1; top: 4096 }
            }

            Text { text: qsTr("Usuário"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
            AppInput {
                id: machineUser
                Layout.fillWidth: true
                placeholderText: qsTr("usuário padrão do host")
                enabled: !backend.machines.createActive
            }

            Text { text: qsTr("Senha do usuário"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
            AppInput {
                id: machinePassword
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: qsTr("opcional; gravada só como hash")
                enabled: !backend.machines.createActive
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
            enabled: !backend.machines.createActive
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

        AppCheck {
            id: isolatedBox
            text: qsTr("Modo isolado (sem mounts, agent SSH nem rede para o host)")
            enabled: !backend.machines.createActive
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
            enabled: !backend.machines.createActive && !isolatedBox.checked
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

        AppCheck {
            id: isolateNetworkBox
            text: qsTr("Isolar a rede da machine")
            enabled: !backend.machines.createActive && !isolatedBox.checked
        }

        AppCheck {
            id: forwardSshAgentBox
            text: qsTr("Encaminhar o SSH agent")
            enabled: !backend.machines.createActive && !isolatedBox.checked
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: backend.machines.createActive || backend.machines.createError.length > 0
                     || backend.machines.createStage === "cancelled"

            Text {
                Layout.fillWidth: true
                text: root.stageLabel(backend.machines.createStage)
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
            text: backend.machines.createActive ? qsTr("Cancelar criação") : qsTr("Fechar")
            variant: "secondary"
            onClicked: {
                if (backend.machines.createActive) {
                    backend.machines.cancelCreate()
                } else {
                    root.close()
                }
            }
        }
        AppButton {
            text: qsTr("Criar")
            variant: "primary"
            enabled: !backend.machines.createActive && machineName.text.length > 0
                     && root.distroLabels.length > 0
            onClicked: root.createRequested()
        }
    }
}
