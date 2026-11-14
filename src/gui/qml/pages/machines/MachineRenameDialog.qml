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
        if (renameName.text.trim().length > 0 && control.machineId.length > 0) {
            backend.machines.rename(control.machineId, renameName.text.trim())
            control.close()
        }
    }

    footer: AppFooter {
        Item { Layout.fillWidth: true }
        AppButton {
            text: qsTr("Cancelar")
            variant: "secondary"
            onClicked: control.close()
        }
        AppButton {
            text: qsTr("Renomear")
            variant: "primary"
            enabled: renameName.text.trim().length > 0 && control.machineId.length > 0
            onClicked: control.submit()
        }
    }
}
