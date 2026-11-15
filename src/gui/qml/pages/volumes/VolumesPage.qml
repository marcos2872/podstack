import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

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

    AppViewHeader {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.topMargin: 20
        Layout.bottomMargin: 16
        title: qsTr("Volumes")
        subtitle: backend.volumes.items.length + qsTr(" volumes")

        AppButton {
            text: qsTr("Criar")
            variant: "primary"
            iconName: "plus"
            onClicked: {
                volumeName.text = ""
                createDialog.open()
            }
        }

        AppButton {
            text: qsTr("Clone")
            enabled: volumesList.currentRow >= 0
            onClicked: {
                cloneSource.text = backend.volumes.items[volumesList.currentRow].name
                cloneDestination.text = cloneSource.text + "-copy"
                cloneDialog.open()
            }
        }

        AppButton {
            text: qsTr("Importar")
            enabled: volumesList.currentRow >= 0
            onClicked: {
                importVolumeName.text = backend.volumes.items[volumesList.currentRow].name
                importHostSource.text = ""
                importHostDialog.open()
            }
        }

        AppButton {
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

        AppButton {
            visible: volumeSelection.count > 0
            text: qsTr("Remover")
            variant: "danger"
            iconName: "trash"
            onClicked: batchRemoveDialog.open()
            Accessible.name: qsTr("Remover volumes selecionados")
        }

        AppButton {
            visible: volumeSelection.count === 0
            text: qsTr("Remover")
            variant: "danger"
            enabled: volumesList.currentRow >= 0
            onClicked: removeDialog.open()
        }

        AppButton {
            variant: "ghost"
            iconName: "refresh"
            onClicked: backend.volumes.refresh()
            Accessible.name: qsTr("Atualizar")
        }
    }

    AppErrorBanner {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 8
        message: backend.volumes.lastError
        actionText: qsTr("Dispensar")
        onActionClicked: backend.volumes.dismissError()
    }

    AppCatalogList {
        id: volumesList
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 20
        emptyIcon: "volumes"
            emptyText: qsTr("Nenhum volume.\nCrie um volume para persistir dados.")
        items: backend.volumes.items
        screenState: backend.volumes.loading ? "loading"
                     : backend.volumes.items.length > 0 ? "ready"
                     : backend.volumes.lastError.length > 0 ? "error" : "empty"
        errorText: backend.volumes.lastError
        retrying: backend.volumes.loading
        onRetryRequested: backend.volumes.refresh()

        selection: volumeSelection
        idKey: "name"
        columns: [
            { key: "name", label: "NAME", width: 0 },
            { key: "driver", label: "DRIVER", width: 80 },
            { key: "size", label: "SIZE", width: 80 },
            { key: "links", label: "LINKS", width: 60 }
        ]
    }

    AppModal {
        id: createDialog
        title: qsTr("Novo volume")
        modal: true
        actionController: backend.volumes
        width: 440
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: qsTr("Nome")
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            AppInput {
                id: volumeName
                Layout.fillWidth: true
                placeholderText: "dados"
            }
            Label {
                text: qsTr("Driver")
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            AppInput {
                id: volumeDriver
                Layout.fillWidth: true
                placeholderText: "local (padrão)"
            }
        }
        footer: AppFooter {
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                enabled: !createDialog.busy
                onClicked: createDialog.close()
            }
            AppButton {
                text: qsTr("Criar")
                busy: createDialog.busy
                busyText: qsTr("Criando…")
                enabled: !createDialog.busy && volumeName.text.length > 0
                onClicked: createDialog.runAction(function() {
                    backend.volumes.createVolume(volumeName.text, volumeDriver.text)
                }, "volumes.create")
            }
        }
    }

    AppModal {
        id: cloneDialog
        title: qsTr("Clonar volume")
        modal: true
        actionController: backend.volumes
        width: 440
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: qsTr("Origem")
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            AppInput {
                id: cloneSource
                Layout.fillWidth: true
                readOnly: true
            }
            Label {
                text: qsTr("Destino (novo volume)")
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            AppInput {
                id: cloneDestination
                Layout.fillWidth: true
                placeholderText: "dados-copy"
            }
        }
        footer: AppFooter {
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                enabled: !cloneDialog.busy
                onClicked: cloneDialog.close()
            }
            AppButton {
                text: qsTr("Clonar")
                busy: cloneDialog.busy
                busyText: qsTr("Clonando…")
                enabled: !cloneDialog.busy && cloneDestination.text.length > 0
                onClicked: cloneDialog.runAction(function() {
                    backend.volumes.cloneVolume(cloneSource.text, cloneDestination.text)
                }, "volumes.clone")
            }
        }
    }

    AppModal {
        id: importHostDialog
        title: qsTr("Importar do host")
        modal: true
        actionController: backend.volumes
        width: 460
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: qsTr("Volume")
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            AppInput {
                id: importVolumeName
                Layout.fillWidth: true
                readOnly: true
            }
            Label {
                text: qsTr("Diretório de origem")
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            AppPathField {
                id: importHostSource
                Layout.fillWidth: true
                title: qsTr("Diretório de origem")
                mode: "directory"
                placeholderText: "/home/usuario/dados"
            }
            Label {
                text: qsTr("O conteúdo será copiado para o volume (aditivo).")
                color: Theme.textSecondary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
        footer: AppFooter {
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                enabled: !importHostDialog.busy
                onClicked: importHostDialog.close()
            }
            AppButton {
                text: qsTr("Importar")
                busy: importHostDialog.busy
                busyText: qsTr("Importando…")
                enabled: !importHostDialog.busy && importHostSource.text.length > 0
                onClicked: importHostDialog.runAction(function() {
                    backend.volumes.importHostPath(importVolumeName.text, importHostSource.text)
                }, "volumes.importHostPath")
            }
        }
    }

    AppConfirmDialog {
        id: removeDialog
        title: qsTr("Remover volume?")
        message: qsTr("O volume e seus dados serão removidos permanentemente.")
        confirmText: qsTr("Remover")
        destructive: true
        onConfirmed: {
            if (volumesList.currentRow >= 0) {
                backend.volumes.removeVolume(backend.volumes.items[volumesList.currentRow].name, false)
            }
        }
    }

    AppModal {
        id: pruneDialog
        title: qsTr("Limpar volumes?")
        modal: true
        implicitWidth: 560
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: qsTr("Remove volumes sem containers usando. Os dados desses volumes serão perdidos.")
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
        footer: AppFooter {
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                onClicked: pruneDialog.close()
            }
            AppButton {
                text: qsTr("Limpar")
                variant: "danger"
                onClicked: {
                    pruneDialog.close()
                    backend.volumes.pruneVolumes()
                }
            }
        }
    }

    AppConfirmDialog {
        id: batchRemoveDialog
        title: qsTr("Remover volumes selecionados")
        message: qsTr("%1 volume(s) serão removidos com os dados. Esta ação não pode ser desfeita.")
                     .arg(volumeSelection.count)
        confirmText: qsTr("Remover")
        destructive: true
        busyText: qsTr("Removendo…")
        actionController: backend.volumes
        actionMethod: "volumes.batch"
        onConfirmed: backend.volumes.batchVolumes(volumeSelection.ids(), false)
    }
}
