import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Renomeia uma machine existente.
AppModal {
    id: control

    property string machineId: ""

    title: qsTr("Renomear machine")
    modal: true
    width: 440
    anchors.centerIn: parent
    actionController: backend.machines

    onOpened: renameName.text = ""

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
            onAccepted: control.submit()
        }
    }

    function submit() {
        if (renameName.text.trim().length === 0 || control.machineId.length === 0) {
            return
        }
        // Só fecha no sucesso: nome inválido/duplicado mantém o diálogo aberto
        // com o erro no header (antes fechava como se tivesse renomeado).
        control.runAction(function() {
            backend.machines.rename(control.machineId, renameName.text.trim())
        }, "machines.rename")
    }

    footer: AppFooter {
        Item { Layout.fillWidth: true }
        AppButton {
            text: qsTr("Cancelar")
            variant: "secondary"
            enabled: !control.busy
            onClicked: control.close()
        }
        AppButton {
            text: qsTr("Renomear")
            variant: "primary"
            busy: control.busy
            busyText: qsTr("Renomeando…")
            enabled: !control.busy && renameName.text.trim().length > 0
                     && control.machineId.length > 0
            onClicked: control.submit()
        }
    }
}
