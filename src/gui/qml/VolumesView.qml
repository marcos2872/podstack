import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: volumesView


    spacing: 0

    ViewHeader {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.topMargin: 20
        Layout.bottomMargin: 16
        title: qsTr("Volumes")
        subtitle: backend.volumes.length + qsTr(" volumes")

        ActionButton {
            text: qsTr("Criar")
            variant: "primary"
            iconName: "plus"
            onClicked: {
                volumeName.text = ""
                createDialog.open()
            }
        }

        ActionButton {
            text: qsTr("Clone")
            enabled: volumesList.currentRow >= 0
            onClicked: {
                cloneSource.text = backend.volumes[volumesList.currentRow].name
                cloneDestination.text = cloneSource.text + "-copy"
                cloneDialog.open()
            }
        }

        ActionButton {
            text: qsTr("Importar")
            enabled: volumesList.currentRow >= 0
            onClicked: {
                importVolumeName.text = backend.volumes[volumesList.currentRow].name
                importHostSource.text = ""
                importHostDialog.open()
            }
        }

        ActionButton {
            text: qsTr("Prune")
            iconName: "trash"
            onClicked: pruneDialog.open()
        }

        ActionButton {
            text: qsTr("Remover")
            variant: "danger"
            enabled: volumesList.currentRow >= 0
            onClicked: removeDialog.open()
        }

        ActionButton {
            variant: "ghost"
            iconName: "refresh"
            onClicked: backend.refreshVolumes()
            Accessible.name: qsTr("Atualizar")
        }
    }

    CatalogList {
        id: volumesList
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 20
        emptyIcon: "volumes"
            emptyText: qsTr("Nenhum volume.\nCrie um volume para persistir dados.")
        items: backend.volumes
        columns: [
            { key: "name", label: "NAME", width: 0 },
            { key: "driver", label: "DRIVER", width: 80 },
            { key: "size", label: "SIZE", width: 80 },
            { key: "links", label: "LINKS", width: 60 }
        ]
    }

    Dialog {
        id: createDialog
        title: "Novo volume"
        modal: true
        width: 440
        background: Rectangle {
            color: Theme.bgSurface
            radius: 10
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: "Nome"
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            FormField {
                id: volumeName
                Layout.fillWidth: true
                placeholderText: "dados"
            }
            Label {
                text: "Driver"
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            FormField {
                id: volumeDriver
                Layout.fillWidth: true
                placeholderText: "local (padrão)"
            }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: "Cancelar"
                variant: "secondary"
                onClicked: createDialog.close()
            }
            ActionButton {
                text: "Criar"
                enabled: volumeName.text.length > 0
                onClicked: {
                    backend.createVolume(volumeName.text, volumeDriver.text)
                    createDialog.close()
                }
            }
        }
    }

    Dialog {
        id: cloneDialog
        title: "Clonar volume"
        modal: true
        width: 440
        background: Rectangle {
            color: Theme.bgSurface
            radius: 10
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: "Origem"
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            FormField {
                id: cloneSource
                Layout.fillWidth: true
                readOnly: true
            }
            Label {
                text: "Destino (novo volume)"
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            FormField {
                id: cloneDestination
                Layout.fillWidth: true
                placeholderText: "dados-copy"
            }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: "Cancelar"
                variant: "secondary"
                onClicked: cloneDialog.close()
            }
            ActionButton {
                text: "Clonar"
                enabled: cloneDestination.text.length > 0
                onClicked: {
                    backend.cloneVolume(cloneSource.text, cloneDestination.text)
                    cloneDialog.close()
                }
            }
        }
    }

    Dialog {
        id: importHostDialog
        title: "Importar do host"
        modal: true
        width: 460
        background: Rectangle {
            color: Theme.bgSurface
            radius: 10
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: "Volume"
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            FormField {
                id: importVolumeName
                Layout.fillWidth: true
                readOnly: true
            }
            Label {
                text: "Diretório de origem"
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            FormField {
                id: importHostSource
                Layout.fillWidth: true
                placeholderText: "/home/usuario/dados"
            }
            Label {
                text: "O conteúdo será copiado para o volume (aditivo)."
                color: Theme.textSecondary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: "Cancelar"
                variant: "secondary"
                onClicked: importHostDialog.close()
            }
            ActionButton {
                text: "Importar"
                enabled: importHostSource.text.length > 0
                onClicked: {
                    backend.importHostPath(importVolumeName.text, importHostSource.text)
                    importHostDialog.close()
                }
            }
        }
    }

    Dialog {
        id: removeDialog
        title: "Remover volume?"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle {
            color: Theme.bgSurface
            radius: 10
            border.color: Theme.border
        }
        contentItem: Label {
            text: "O volume e seus dados serão removidos permanentemente."
            color: Theme.textPrimary
            wrapMode: Text.WordWrap
        }
        onAccepted: {
            if (volumesList.currentRow >= 0) {
                backend.removeVolume(backend.volumes[volumesList.currentRow].name, false)
            }
        }
    }

    Dialog {
        id: pruneDialog
        title: "Limpar volumes?"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle {
            color: Theme.bgSurface
            radius: 10
            border.color: Theme.border
        }
        contentItem: Label {
            text: "Remove volumes sem containers usando. Os dados desses volumes serão perdidos."
            color: Theme.textPrimary
            wrapMode: Text.WordWrap
        }
        onAccepted: backend.pruneVolumes()
    }
}
