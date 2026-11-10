import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: volumesView

    readonly property var pruneVolumeSection: backend.prunePreview.volumes
                                             ? backend.prunePreview.volumes : null
    readonly property var pruneVolumeItems: pruneVolumeSection && pruneVolumeSection.items
                                            ? pruneVolumeSection.items : []

    function formatBytes(bytes) {
        var value = Number(bytes) || 0
        var units = ["B", "KiB", "MiB", "GiB", "TiB"]
        var unit = 0
        while (value >= 1024 && unit < units.length - 1) {
            value /= 1024
            ++unit
        }
        return (unit === 0 ? value.toFixed(0) : value.toFixed(1)) + " " + units[unit]
    }

    spacing: 0

    SelectionState { id: volumeSelection }

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

        Text {
            visible: volumeSelection.count > 0
            text: volumeSelection.count + qsTr(" selecionados")
            color: Theme.indigo400
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMeta
        }

        ActionButton {
            visible: volumeSelection.count > 0
            text: qsTr("Remover")
            variant: "danger"
            iconName: "trash"
            onClicked: backend.batchVolumes(volumeSelection.ids(), false)
            Accessible.name: qsTr("Remover volumes selecionados")
        }

        ActionButton {
            visible: volumeSelection.count === 0
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
        selection: volumeSelection
        idKey: "name"
        columns: [
            { key: "name", label: "NAME", width: 0 },
            { key: "driver", label: "DRIVER", width: 80 },
            { key: "size", label: "SIZE", width: 80 },
            { key: "links", label: "LINKS", width: 60 }
        ]
    }

    AppDialog {
        id: createDialog
        title: "Novo volume"
        modal: true
        width: 440
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
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
        footer: AppFooter {
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

    AppDialog {
        id: cloneDialog
        title: "Clonar volume"
        modal: true
        width: 440
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
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
        footer: AppFooter {
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

    AppDialog {
        id: importHostDialog
        title: "Importar do host"
        modal: true
        width: 460
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
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
            PathField {
                id: importHostSource
                Layout.fillWidth: true
                title: qsTr("Diretório de origem")
                mode: "directory"
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
        footer: AppFooter {
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

    AppDialog {
        id: removeDialog
        title: "Remover volume?"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
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

    AppDialog {
        id: pruneDialog
        title: "Limpar volumes?"
        modal: true
        implicitWidth: 560
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: "Remove volumes sem containers usando. Os dados desses volumes serão perdidos."
                color: Theme.textPrimary
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Espaço recuperável estimado: %1").arg(
                          volumesView.formatBytes(volumesView.pruneVolumeSection
                                                  ? volumesView.pruneVolumeSection.reclaimableBytes : 0))
                color: Theme.cyan
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
                font.bold: true
            }

            ListView {
                id: pruneVolumeList
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? Math.min(180, Math.max(44, contentHeight)) : 0
                visible: volumesView.pruneVolumeItems.length > 0
                clip: true
                model: volumesView.pruneVolumeItems
                spacing: 4

                delegate: RowLayout {
                    width: pruneVolumeList.width
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        text: modelData.name
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideMiddle
                    }

                    Label {
                        text: modelData.inUse ? qsTr("em uso · não será removido")
                                              : qsTr("sem uso · será removido")
                        color: modelData.inUse ? Theme.textTertiary : Theme.danger
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }

                    Label {
                        text: volumesView.formatBytes(modelData.sizeBytes)
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMeta
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: volumesView.pruneVolumeItems.length === 0
                text: qsTr("Nenhum volume encontrado.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
            }
        }
        onOpened: backend.refreshDiskUsage()
        onAccepted: backend.pruneVolumes()
    }
}
