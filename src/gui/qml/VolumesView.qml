import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: volumesView

    Theme { id: theme }

    spacing: 0

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 52
        color: theme.bgBase

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 8

            Label {
                text: "Volumes"
                color: theme.textPrimary
                font.pixelSize: 15
                font.bold: true
            }

            Label {
                text: backend.volumes.length + " itens"
                color: theme.textSecondary
                font.pixelSize: 11
            }

            Item { Layout.fillWidth: true }

            ActionButton {
                text: "Criar"
                onClicked: {
                    volumeName.text = ""
                    createDialog.open()
                }
            }

            ActionButton {
                text: "Clone"
                accentColor: theme.indigo500
                enabled: volumesList.currentRow >= 0
                onClicked: {
                    cloneSource.text = backend.volumes[volumesList.currentRow].name
                    cloneDestination.text = cloneSource.text + "-copy"
                    cloneDialog.open()
                }
            }

            ActionButton {
                text: "Importar"
                accentColor: theme.indigo500
                enabled: volumesList.currentRow >= 0
                onClicked: {
                    importVolumeName.text = backend.volumes[volumesList.currentRow].name
                    importHostSource.text = ""
                    importHostDialog.open()
                }
            }

            ActionButton {
                text: "Prune"
                accentColor: theme.warning
                onClicked: pruneDialog.open()
            }

            ActionButton {
                text: "Remover"
                accentColor: theme.danger
                enabled: volumesList.currentRow >= 0
                onClicked: removeDialog.open()
            }

            ActionButton {
                text: "Atualizar"
                accentColor: theme.bgElevated
                onClicked: backend.refreshVolumes()
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: theme.border
    }

    CatalogList {
        id: volumesList
        Layout.fillWidth: true
        Layout.fillHeight: true
        items: backend.volumes
        columns: [
            { key: "name", label: "NAME", width: 0 },
            { key: "driver", label: "DRIVER", width: 80 },
            { key: "size", label: "SIZE", width: 80 },
            { key: "links", label: "LINKS", width: 60 }
        ]
    }

    Label {
        Layout.fillWidth: true
        Layout.margins: 24
        visible: backend.volumes.length === 0
        horizontalAlignment: Text.AlignHCenter
        text: "Nenhum volume.\nCrie um volume para persistir dados."
        color: theme.textSecondary
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }

    Dialog {
        id: createDialog
        title: "Novo volume"
        modal: true
        width: 440
        background: Rectangle {
            color: theme.bgSurface
            radius: 10
            border.color: theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: "Nome"
                color: theme.textSecondary
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
                color: theme.textSecondary
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
                accentColor: theme.bgElevated
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
            color: theme.bgSurface
            radius: 10
            border.color: theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: "Origem"
                color: theme.textSecondary
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
                color: theme.textSecondary
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
                accentColor: theme.bgElevated
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
            color: theme.bgSurface
            radius: 10
            border.color: theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: "Volume"
                color: theme.textSecondary
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
                color: theme.textSecondary
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
                color: theme.textSecondary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: "Cancelar"
                accentColor: theme.bgElevated
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
            color: theme.bgSurface
            radius: 10
            border.color: theme.border
        }
        contentItem: Label {
            text: "O volume e seus dados serão removidos permanentemente."
            color: theme.textPrimary
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
            color: theme.bgSurface
            radius: 10
            border.color: theme.border
        }
        contentItem: Label {
            text: "Remove volumes sem containers usando. Os dados desses volumes serão perdidos."
            color: theme.textPrimary
            wrapMode: Text.WordWrap
        }
        onAccepted: backend.pruneVolumes()
    }
}
