import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: imagesView

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
                text: "Images"
                color: theme.textPrimary
                font.pixelSize: 15
                font.bold: true
            }

            Label {
                text: backend.images.length + " itens"
                color: theme.textSecondary
                font.pixelSize: 11
            }

            Item { Layout.fillWidth: true }

            Label {
                text: backend.pullStatus
                visible: backend.pullStatus.length > 0
                color: backend.pullActive ? theme.cyan : theme.textSecondary
                font.pixelSize: 10
                elide: Text.ElideRight
                Layout.maximumWidth: 280
            }

            ActionButton {
                text: "Pull"
                onClicked: {
                    pullReference.text = ""
                    pullDialog.open()
                }
            }

            ActionButton {
                text: "Importar"
                accentColor: theme.bgElevated
                onClicked: {
                    importPath.text = ""
                    importDialog.open()
                }
            }

            ActionButton {
                text: "Prune"
                accentColor: theme.warning
                onClicked: pruneDialog.open()
            }

            ActionButton {
                text: "Tag"
                accentColor: theme.indigo500
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
                text: "Untag"
                accentColor: theme.bgElevated
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
                text: "Remover"
                accentColor: theme.danger
                enabled: imagesList.currentRow >= 0
                onClicked: removeDialog.open()
            }

            ActionButton {
                text: "Atualizar"
                accentColor: theme.bgElevated
                onClicked: backend.refreshImages()
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: theme.border
    }

    CatalogList {
        id: imagesList
        Layout.fillWidth: true
        Layout.fillHeight: true
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

    Label {
        Layout.fillWidth: true
        Layout.margins: 24
        visible: backend.images.length === 0
        horizontalAlignment: Text.AlignHCenter
        text: "Nenhuma imagem.\nUse Pull para baixar uma imagem."
        color: theme.textSecondary
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }

    Dialog {
        id: pullDialog
        title: "Pull de imagem"
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
                text: "Imagem"
                color: theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }

            FormField {
                id: pullReference
                Layout.fillWidth: true
                placeholderText: "nginx:alpine"
            }

            FormField {
                id: pullPlatform
                Layout.fillWidth: true
                placeholderText: "plataforma (opcional, ex.: linux/arm64)"
            }

            Label {
                Layout.fillWidth: true
                visible: backend.pullActive || backend.pullStatus.length > 0
                text: backend.pullStatus
                color: theme.textSecondary
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
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: backend.pullActive ? "Cancelar" : "Fechar"
                accentColor: theme.bgElevated
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
                    backend.pullImage(pullReference.text, pullPlatform.text)
                }
            }
        }
    }

    Dialog {
        id: importDialog
        title: "Importar imagem"
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
                text: "Arquivo tar"
                color: theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            FormField {
                id: importPath
                Layout.fillWidth: true
                placeholderText: "/caminho/imagem.tar"
            }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: "Cancelar"
                accentColor: theme.bgElevated
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

    Dialog {
        id: removeDialog
        title: "Remover imagem?"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle {
            color: theme.bgSurface
            radius: 10
            border.color: theme.border
        }
        contentItem: Label {
            text: "A imagem será removida. Containers em uso bloqueiam a remoção."
            color: theme.textPrimary
            wrapMode: Text.WordWrap
        }
        onAccepted: {
            if (imagesList.currentRow >= 0) {
                backend.removeImage(backend.images[imagesList.currentRow].id, false)
            }
        }
    }

    Dialog {
        id: pruneDialog
        title: "Limpar imagens?"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle {
            color: theme.bgSurface
            radius: 10
            border.color: theme.border
        }
        contentItem: Label {
            text: "Remove imagens sem uso (dangling). Containers existentes não são afetados."
            color: theme.textPrimary
            wrapMode: Text.WordWrap
        }
        onAccepted: backend.pruneImages(false)
    }

    Dialog {
        id: tagDialog
        title: "Tag da imagem"
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
                id: tagName
                text: ""
                color: theme.textSecondary
                font.pixelSize: 11
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            Label {
                text: "Nova referência"
                color: theme.textSecondary
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
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: "Cancelar"
                accentColor: theme.bgElevated
                onClicked: tagDialog.close()
            }
            ActionButton {
                text: "Remover tag"
                accentColor: theme.warning
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
