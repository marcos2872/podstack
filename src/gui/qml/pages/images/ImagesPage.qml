import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

ColumnLayout {
    id: imagesView

    readonly property var pruneImageSection: backend.prunePreview.images
                                             ? backend.prunePreview.images : null
    readonly property var pruneImageItems: pruneImageSection && pruneImageSection.items
                                           ? pruneImageSection.items : []

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

    function imageName(item) {
        var repository = item.repository || ""
        var tag = item.tag || ""
        if (!repository || repository === "<none>") {
            return "<none>:<none>"
        }
        return tag.length > 0 ? repository + ":" + tag : repository
    }

    function imageFlags(item) {
        var flags = []
        if (item.dangling) {
            flags.push(qsTr("dangling"))
        }
        flags.push(item.unused ? qsTr("sem uso") : qsTr("em uso"))
        return flags.join(" · ")
    }

    spacing: 0

    SelectionState { id: imageSelection }

    AppViewHeader {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.topMargin: 20
        Layout.bottomMargin: 16
        title: qsTr("Images")
        subtitle: backend.images.items.length + qsTr(" imagens")

        Text {
            visible: backend.images.pullStatus.length > 0
            text: backend.images.pullStatus
            color: backend.images.pullActive ? Theme.cyan : Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            elide: Text.ElideRight
            Layout.maximumWidth: 260
        }

        AppButton {
            text: qsTr("Pull")
            variant: "primary"
            iconName: "download"
            onClicked: {
                pullReference.text = ""
                pullDialog.open()
            }
        }

        AppButton {
            text: qsTr("Importar")
            iconName: "upload"
            onClicked: {
                importPath.text = ""
                importDialog.open()
            }
        }

        AppButton {
            text: qsTr("Prune")
            iconName: "trash"
            onClicked: pruneDialog.open()
        }

        AppButton {
            text: qsTr("Tag")
            enabled: imagesList.currentRow >= 0
            onClicked: {
                const image = backend.images.items[imagesList.currentRow]
                tagName.text = image.name
                tagId.text = image.id
                tagRepo.text = image.name
                tagDialog.open()
            }
        }

        AppButton {
            text: qsTr("Untag")
            enabled: imagesList.currentRow >= 0
            onClicked: {
                const image = backend.images.items[imagesList.currentRow]
                tagName.text = image.name
                tagId.text = image.id
                tagRepo.text = image.name
                tagDialog.open()
            }
        }

        Text {
            visible: imageSelection.count > 0
            text: imageSelection.count + qsTr(" selecionadas")
            color: Theme.indigo400
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMeta
        }

        AppButton {
            visible: imageSelection.count > 0
            text: qsTr("Remover")
            variant: "danger"
            iconName: "trash"
            onClicked: batchRemoveDialog.open()
            Accessible.name: qsTr("Remover imagens selecionadas")
        }

        AppButton {
            visible: imageSelection.count === 0
            text: qsTr("Remover")
            variant: "danger"
            enabled: imagesList.currentRow >= 0
            onClicked: removeDialog.open()
        }

        AppButton {
            variant: "ghost"
            iconName: "refresh"
            onClicked: backend.images.refresh()
            Accessible.name: qsTr("Atualizar")
        }
    }

    AppErrorBanner {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 8
        message: backend.images.lastError
        actionText: qsTr("Dispensar")
        onActionClicked: backend.images.dismissError()
    }

    AppCatalogList {
        id: imagesList
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 20
        emptyIcon: "images"
            emptyText: qsTr("Nenhuma imagem.\nUse Pull para baixar uma imagem.")
        items: backend.images.items
        screenState: backend.images.loading ? "loading"
                     : backend.images.items.length > 0 ? "ready"
                     : backend.images.lastError.length > 0 ? "error" : "empty"
        errorText: backend.images.lastError
        retrying: backend.images.loading
        onRetryRequested: backend.images.refresh()

        selection: imageSelection
        idKey: "id"
        columns: [
            { key: "name", label: "REPOSITORY:TAG", width: 0 },
            { key: "digest", label: "DIGEST", width: 150, mono: true },
            { key: "size", label: "SIZE", width: 80 },
            { key: "containers", label: "USO", width: 60 }
        ]
        onActivated: function(index) {
            tagId.text = backend.images.items[index].id
            tagName.text = backend.images.items[index].name
        }
    }

    AppModal {
        id: pullDialog
        title: qsTr("Pull de imagem")
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
                text: qsTr("Imagem")
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }

            AppInput {
                id: pullReference
                Layout.fillWidth: true
                placeholderText: "nginx:alpine"
            }

            AppSelect {
                id: pullPlatform
                Layout.fillWidth: true
                model: [qsTr("Automático"), "linux/amd64", "linux/arm64",
                        "linux/arm/v7", "linux/ppc64le", "linux/s390x"]
                currentIndex: 0
            }

            Label {
                Layout.fillWidth: true
                visible: backend.images.pullActive || backend.images.pullStatus.length > 0
                text: backend.images.pullStatus
                color: Theme.textSecondary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }

            ProgressBar {
                Layout.fillWidth: true
                indeterminate: backend.images.pullActive
                visible: backend.images.pullActive
                from: 0
                to: 1
            }
        }
        footer: AppFooter {
            Item { Layout.fillWidth: true }
            AppButton {
                text: backend.images.pullActive ? "Cancelar" : "Fechar"
                variant: "secondary"
                onClicked: {
                    if (backend.images.pullActive) {
                        backend.images.cancelPull()
                    }
                    pullDialog.close()
                }
            }
            AppButton {
                text: qsTr("Pull")
                enabled: pullReference.text.length > 0 && !backend.images.pullActive
                onClicked: {
                    backend.images.pullImage(pullReference.text,
                                      pullPlatform.currentIndex > 0
                                      ? pullPlatform.currentText : "")
                }
            }
        }
    }

    AppModal {
        id: importDialog
        title: qsTr("Importar imagem")
        modal: true
        width: 460
        actionController: backend.images
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: qsTr("Arquivo tar")
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            AppPathField {
                id: importPath
                Layout.fillWidth: true
                title: qsTr("Importar imagem")
                filter: qsTr("Imagens (*.tar *.tar.gz *.tar.zst);;Todos os arquivos (*)")
                placeholderText: "/caminho/imagem.tar"
            }
        }
        footer: AppFooter {
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                enabled: !importDialog.busy
                onClicked: importDialog.close()
            }
            AppButton {
                text: qsTr("Importar")
                busy: importDialog.busy
                busyText: qsTr("Importando…")
                enabled: !importDialog.busy && importPath.text.length > 0
                onClicked: importDialog.runAction(function() {
                    backend.images.loadImage(importPath.text)
                }, "images.load")
            }
        }
    }

    AppConfirmDialog {
        id: removeDialog
        title: qsTr("Remover imagem?")
        message: qsTr("A imagem será removida. Containers em uso bloqueiam a remoção.")
        confirmText: qsTr("Remover")
        destructive: true
        onConfirmed: {
            if (imagesList.currentRow >= 0) {
                backend.images.removeImage(backend.images.items[imagesList.currentRow].id, false)
            }
        }
    }

    AppModal {
        id: pruneDialog
        title: qsTr("Limpar imagens?")
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
                text: qsTr("Remove imagens sem uso (dangling). Containers existentes não são afetados.")
                color: Theme.textPrimary
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Total recuperável estimado: %1").arg(
                          imagesView.formatBytes(imagesView.pruneImageSection
                                                 ? imagesView.pruneImageSection.reclaimableBytes : 0))
                color: Theme.cyan
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
                font.bold: true
            }

            ListView {
                id: pruneImageList
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? Math.min(180, Math.max(44, contentHeight)) : 0
                visible: imagesView.pruneImageItems.length > 0
                clip: true
                model: imagesView.pruneImageItems
                spacing: 4

                delegate: RowLayout {
                    width: pruneImageList.width
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        text: imagesView.imageName(modelData)
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideMiddle
                    }

                    Label {
                        text: imagesView.imageFlags(modelData)
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }

                    Label {
                        text: imagesView.formatBytes(modelData.sizeBytes)
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMeta
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: imagesView.pruneImageItems.length === 0
                text: qsTr("Nenhuma imagem sem uso encontrada.")
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
                    backend.images.pruneImages(false)
                }
            }
        }
    }

    AppModal {
        id: tagDialog
        title: qsTr("Tag da imagem")
        modal: true
        width: 460
        actionController: backend.images
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                id: tagName
                text: ""
                color: Theme.textSecondary
                font.pixelSize: 11
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("Nova referência")
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            AppInput {
                id: tagRepo
                Layout.fillWidth: true
                placeholderText: "repo:tag"
            }
            Label {
                id: tagId
                text: ""
                visible: false
            }
        }
        footer: AppFooter {
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                enabled: !tagDialog.busy
                onClicked: tagDialog.close()
            }
            AppButton {
                text: qsTr("Remover tag")
                variant: "danger"
                busy: tagDialog.busy
                busyText: qsTr("Removendo…")
                enabled: !tagDialog.busy && tagRepo.text.length > 0
                onClicked: tagDialog.runAction(function() {
                    backend.images.untagImage(tagId.text, tagRepo.text)
                }, "images.untag")
            }
            AppButton {
                text: qsTr("Aplicar")
                busy: tagDialog.busy
                busyText: qsTr("Aplicando…")
                enabled: !tagDialog.busy && tagRepo.text.length > 0
                onClicked: tagDialog.runAction(function() {
                    backend.images.tagImage(tagId.text, tagRepo.text)
                }, "images.tag")
            }
        }
    }

    AppConfirmDialog {
        id: batchRemoveDialog
        title: qsTr("Remover imagens selecionadas")
        message: qsTr("%1 imagem(ns) serão removidas. Containers que as usam podem impedir a remoção.")
                     .arg(imageSelection.count)
        confirmText: qsTr("Remover")
        destructive: true
        busyText: qsTr("Removendo…")
        actionController: backend.images
        actionMethod: "images.batch"
        onConfirmed: backend.images.batchImages(imageSelection.ids(), false)
    }
}
