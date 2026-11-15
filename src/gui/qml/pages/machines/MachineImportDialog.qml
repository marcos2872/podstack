import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Importação de machine a partir de um .tar.zst.
AppModal {
    id: control

    /// Função de rótulo de etapa, compartilhada pela página.
    property var stageLabel: function(stage) { return stage }

    /// Abre limpando o formulário. A página não pode tocar nos campos
    /// internos: ids de um arquivo filho não existem no escopo do pai (era o
    /// que impedia o diálogo de abrir).
    function openDialog() {
        importPath.text = ""
        importName.text = ""
        importForce.checked = false
        backend.machines.resetCreateState()
        control.open()
    }

    title: qsTr("Importar machine")
    modal: true
    width: 520
    anchors.centerIn: parent
    closePolicy: backend.machines.createActive ? Popup.NoAutoClose
                                               : Popup.CloseOnEscape | Popup.CloseOnPressOutside

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
                text: control.stageLabel(backend.machines.createStage)
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
                    backend.machines.cancelCreate("import")
                } else {
                    control.close()
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
