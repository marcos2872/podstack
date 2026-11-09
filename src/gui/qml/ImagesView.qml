import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

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

    ViewHeader {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.topMargin: 20
        Layout.bottomMargin: 16
        title: qsTr("Images")
        subtitle: backend.images.length + qsTr(" imagens")

        Text {
            visible: backend.pullStatus.length > 0
            text: backend.pullStatus
            color: backend.pullActive ? Theme.cyan : Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            elide: Text.ElideRight
            Layout.maximumWidth: 260
        }

        ActionButton {
            text: qsTr("Pull")
            variant: "primary"
            iconName: "download"
            onClicked: {
                pullReference.text = ""
                pullDialog.open()
            }
        }

        ActionButton {
            text: qsTr("Importar")
            iconName: "upload"
            onClicked: {
                importPath.text = ""
                importDialog.open()
            }
        }

        ActionButton {
            text: qsTr("Prune")
            iconName: "trash"
            onClicked: pruneDialog.open()
        }

        ActionButton {
            text: qsTr("Tag")
            enabled: imagesList.currentRow >= 0
            onClicked: {
                const image = backend.images[imagesList.currentRow]
                tagName.text = image.name
                tagId.text = image.id
                tagRepo.text = image.name
                tagDialog.open()
            }
        }

        ActionButton {
            text: qsTr("Untag")
            enabled: imagesList.currentRow >= 0
            onClicked: {
                const image = backend.images[imagesList.currentRow]
                tagName.text = image.name
                tagId.text = image.id
                tagRepo.text = image.name
                tagDialog.open()
            }
        }

        ActionButton {
            text: qsTr("Remover")
            variant: "danger"
            enabled: imagesList.currentRow >= 0
            onClicked: removeDialog.open()
        }

        ActionButton {
            variant: "ghost"
            iconName: "refresh"
            onClicked: backend.refreshImages()
            Accessible.name: qsTr("Atualizar")
        }
    }

    CatalogList {
        id: imagesList
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 20
        emptyIcon: "images"
            emptyText: qsTr("Nenhuma imagem.\nUse Pull para baixar uma imagem.")
        items: backend.images
        columns: [
            { key: "name", label: "REPOSITORY:TAG", width: 0 },
            { key: "digest", label: "DIGEST", width: 150, mono: true },
            { key: "size", label: "SIZE", width: 80 },
            { key: "containers", label: "USO", width: 60 }
        ]
        onActivated: function(index) {
            tagId.text = backend.images[index].id
            tagName.text = backend.images[index].name
        }
    }

    AppDialog {
        id: pullDialog
        title: "Pull de imagem"
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
                text: "Imagem"
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }

            FormField {
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
                visible: backend.pullActive || backend.pullStatus.length > 0
                text: backend.pullStatus
                color: Theme.textSecondary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }

            ProgressBar {
                Layout.fillWidth: true
                indeterminate: backend.pullActive
                visible: backend.pullActive
                from: 0
                to: 1
            }
        }
        footer: AppFooter {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: backend.pullActive ? "Cancelar" : "Fechar"
                variant: "secondary"
                onClicked: {
                    if (backend.pullActive) {
                        backend.cancelPull()
                    }
                    pullDialog.close()
                }
            }
            ActionButton {
                text: "Pull"
                enabled: pullReference.text.length > 0 && !backend.pullActive
                onClicked: {
                    backend.pullImage(pullReference.text,
                                      pullPlatform.currentIndex > 0
                                      ? pullPlatform.currentText : "")
                }
            }
        }
    }

    AppDialog {
        id: importDialog
        title: "Importar imagem"
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
                text: "Arquivo tar"
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            FormField {
                id: importPath
                Layout.fillWidth: true
                placeholderText: "/caminho/imagem.tar"
            }
        }
        footer: AppFooter {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: "Cancelar"
                variant: "secondary"
                onClicked: importDialog.close()
            }
            ActionButton {
                text: "Importar"
                enabled: importPath.text.length > 0
                onClicked: {
                    backend.loadImage(importPath.text)
                    importDialog.close()
                }
            }
        }
    }

    AppDialog {
        id: removeDialog
        title: "Remover imagem?"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.color: Theme.border
        }
        contentItem: Label {
            text: "A imagem será removida. Containers em uso bloqueiam a remoção."
            color: Theme.textPrimary
            wrapMode: Text.WordWrap
        }
        onAccepted: {
            if (imagesList.currentRow >= 0) {
                backend.removeImage(backend.images[imagesList.currentRow].id, false)
            }
        }
    }

    AppDialog {
        id: pruneDialog
        title: "Limpar imagens?"
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
                text: "Remove imagens sem uso (dangling). Containers existentes não são afetados."
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
        onAccepted: backend.pruneImages(false)
    }

    AppDialog {
        id: tagDialog
        title: "Tag da imagem"
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
                id: tagName
                text: ""
                color: Theme.textSecondary
                font.pixelSize: 11
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            Label {
                text: "Nova referência"
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            FormField {
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
            ActionButton {
                text: "Cancelar"
                variant: "secondary"
                onClicked: tagDialog.close()
            }
            ActionButton {
                text: "Remover tag"
                variant: "danger"
                enabled: tagRepo.text.length > 0
                onClicked: {
                    backend.untagImage(tagId.text, tagRepo.text)
                    tagDialog.close()
                }
            }
            ActionButton {
                text: "Aplicar"
                enabled: tagRepo.text.length > 0
                onClicked: {
                    backend.tagImage(tagId.text, tagRepo.text)
                    tagDialog.close()
                }
            }
        }
    }
}
