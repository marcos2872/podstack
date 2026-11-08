import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: filesView

    property string containerId

    spacing: 8

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        Text {
            Layout.fillWidth: true
            text: backend.filesPath
            color: Theme.textSecondary
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fontMeta
            elide: Text.ElideMiddle
        }

        ActionButton {
            text: qsTr("Enviar")
            variant: "primary"
            size: "sm"
            iconName: "upload"
            enabled: filesView.containerId !== "" && uploadPath.text.length > 0
            onClicked: {
                backend.uploadFile(filesView.containerId, backend.filesPath, uploadPath.text)
            }
        }

        ActionButton {
            variant: "ghost"
            iconName: "refresh"
            size: "sm"
            enabled: filesView.containerId !== ""
            onClicked: backend.listFiles(filesView.containerId, backend.filesPath)
            Accessible.name: qsTr("Atualizar")
        }
    }

    FormField {
        id: uploadPath
        Layout.fillWidth: true
        Layout.preferredHeight: 30
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
            model: backend.files
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: fileRow
                width: filesList.width
                height: 34
                hoverEnabled: true

                onClicked: {
                    if (modelData.directory) {
                        return
                    }
                    const base = backend.filesPath.endsWith("/") ? backend.filesPath
                                                                 : backend.filesPath + "/"
                    backend.downloadFile(filesView.containerId, base + modelData.name)
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
            backend.listFiles(containerId, "/")
        }
    }
}
