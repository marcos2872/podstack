import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

ColumnLayout {
    id: filesView

    property string containerId

    readonly property string currentPath: backend.containers.filesPath
    readonly property bool canGoUp: currentPath.length > 1

    function joinPath(base, name) {
        return (base.endsWith("/") ? base : base + "/") + name
    }

    function parentPath() {
        var path = currentPath
        if (path.length <= 1) {
            return "/"
        }
        if (path.endsWith("/")) {
            path = path.slice(0, -1)
        }
        const slash = path.lastIndexOf("/")
        return slash <= 0 ? "/" : path.slice(0, slash)
    }

    function enterDirectory(name) {
        backend.containers.listFiles(filesView.containerId, joinPath(currentPath, name))
    }

    spacing: 8

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        Text {
            Layout.fillWidth: true
            text: backend.containers.filesPath
            color: Theme.textSecondary
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fontMeta
            elide: Text.ElideMiddle
        }

        AppButton {
            text: qsTr("Subir")
            variant: "ghost"
            size: "sm"
            iconName: "arrowUp"
            enabled: filesView.containerId !== "" && filesView.canGoUp
            onClicked: backend.containers.listFiles(filesView.containerId,
                                                    filesView.parentPath())
            Accessible.name: qsTr("Subir um diretório")
        }

        AppButton {
            text: qsTr("Enviar")
            variant: "primary"
            size: "sm"
            iconName: "upload"
            enabled: filesView.containerId !== "" && uploadPath.text.length > 0
            onClicked: {
                backend.containers.uploadFile(filesView.containerId, backend.containers.filesPath, uploadPath.text)
            }
        }

        AppButton {
            variant: "ghost"
            iconName: "refresh"
            size: "sm"
            enabled: filesView.containerId !== ""
            onClicked: backend.containers.listFiles(filesView.containerId, backend.containers.filesPath)
            Accessible.name: qsTr("Atualizar")
        }
    }

    AppPathField {
        id: uploadPath
        Layout.fillWidth: true
        title: qsTr("Enviar arquivo para o container")
        filter: qsTr("Todos os arquivos (*)")
        placeholderText: qsTr("Arquivo local para enviar ao container…")
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        color: Theme.bgSurface2
        radius: Theme.radiusSm
        border.width: 1
        border.color: Theme.border
        clip: true

        ListView {
            id: filesList
            anchors.fill: parent
            clip: true
            model: backend.containers.files
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: fileRow
                width: filesList.width
                height: 34
                hoverEnabled: true

                onClicked: {
                    if (modelData.directory) {
                        // Antes o clique em diretório não fazia nada: só a raiz
                        // era navegável.
                        filesView.enterDirectory(modelData.name)
                        return
                    }
                    backend.containers.downloadFile(filesView.containerId,
                                                    filesView.joinPath(filesView.currentPath,
                                                                       modelData.name))
                }

                background: Rectangle {
                    color: fileRow.hovered
                           ? Qt.rgba(Theme.textPrimary.r, Theme.textPrimary.g,
                                     Theme.textPrimary.b, 0.04)
                           : "transparent"
                }

                contentItem: RowLayout {
                    spacing: 8

                    AppIcon {
                        name: modelData.directory ? "folder" : "file"
                        size: 14
                        color: modelData.directory ? Theme.indigo400 : Theme.textTertiary
                    }

                    Text {
                        Layout.fillWidth: true
                        text: modelData.name
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideMiddle
                    }

                    Text {
                        text: modelData.directory ? qsTr("dir") : filesView.formatSize(modelData.size)
                        color: Theme.textTertiary
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontMeta
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 32
                visible: filesList.count === 0
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Selecione um container e atualize para listar os arquivos.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
                wrapMode: Text.WordWrap
            }
        }
    }

    function formatSize(bytes) {
        if (bytes < 1024) {
            return bytes + " B"
        }
        if (bytes < 1024 * 1024) {
            return (bytes / 1024).toFixed(1) + " KiB"
        }
        return (bytes / (1024 * 1024)).toFixed(1) + " MiB"
    }

    Component.onCompleted: {
        if (containerId !== "") {
            backend.containers.listFiles(containerId, "/")
        }
    }
}
