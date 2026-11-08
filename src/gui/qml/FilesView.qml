import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: filesView

    property string containerId

    Theme { id: theme }

    spacing: 6

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        Label {
            Layout.fillWidth: true
            text: backend.filesPath
            color: theme.textSecondary
            font.family: "monospace"
            font.pixelSize: 10
            elide: Text.ElideMiddle
        }

        ActionButton {
            text: "Atualizar"
            accentColor: theme.bgElevated
            enabled: filesView.containerId !== ""
            onClicked: backend.listFiles(filesView.containerId, backend.filesPath)
        }
    }

    ListView {
        id: filesList
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: backend.files
        ScrollBar.vertical: ScrollBar {}

        delegate: ItemDelegate {
            width: filesList.width
            height: 32
            onClicked: {
                if (model.directory) {
                    return
                }
                const base = backend.filesPath.endsWith("/") ? backend.filesPath
                                                             : backend.filesPath + "/"
                backend.downloadFile(filesView.containerId, base + model.name)
            }
            background: Rectangle {
                color: "transparent"
            }
            contentItem: RowLayout {
                spacing: 8

                Rectangle {
                    width: 14
                    height: 14
                    radius: 3
                    color: model.directory ? theme.indigo700 : theme.bgElevated
                }

                Label {
                    Layout.fillWidth: true
                    text: model.name
                    color: theme.textPrimary
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                }

                Label {
                    text: model.directory ? "dir" : filesView.formatSize(model.size)
                    color: theme.textSecondary
                    font.pixelSize: 10
                }
            }
        }
    }

    Label {
        Layout.fillWidth: true
        visible: backend.files.length === 0
        text: "Selecione um container e atualize para listar os arquivos."
        color: theme.textSecondary
        font.pixelSize: 10
        wrapMode: Text.WordWrap
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
            backend.listFiles(containerId, "/")
        }
    }
}
