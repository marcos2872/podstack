import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

// File manager da machine via SSH/SCP (aba Files do MachineInspector).
// A listagem e as transferências rodam no daemon; aqui só apresentação.
ColumnLayout {
    id: root

    property string machineId

    spacing: 8

    readonly property string currentPath: backend.machineFilesPath.length > 0
                                          ? backend.machineFilesPath : "/"

    function parentPath(path) {
        if (!path || path === "/") {
            return "/"
        }
        let trimmed = path
        while (trimmed.length > 1 && trimmed.endsWith("/")) {
            trimmed = trimmed.substring(0, trimmed.length - 1)
        }
        const index = trimmed.lastIndexOf("/")
        return index <= 0 ? "/" : trimmed.substring(0, index)
    }

    function go(path) {
        if (root.machineId.length === 0) {
            return
        }
        backend.refreshMachineFiles(root.machineId, path.length > 0 ? path : "/")
    }

    function formatSize(bytes) {
        const number = Number(bytes || 0)
        if (number < 1024) {
            return number.toFixed(0) + " B"
        }
        if (number < 1024 * 1024) {
            return (number / 1024).toFixed(1) + " KiB"
        }
        if (number < 1024 * 1024 * 1024) {
            return (number / (1024 * 1024)).toFixed(1) + " MiB"
        }
        return (number / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
    }

    onMachineIdChanged: {
        pathField.text = "/"
        uploadPath.text = ""
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 14
        Layout.rightMargin: 14
        Layout.topMargin: 8
        spacing: 6

        FormField {
            id: pathField
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            placeholderText: qsTr("Caminho remoto…")
            text: backend.machineFilesPath
            onAccepted: root.go(text)
        }

        ActionButton {
            text: qsTr("Ir")
            size: "sm"
            variant: "secondary"
            enabled: root.machineId.length > 0 && !backend.machineFilesBusy
            onClicked: root.go(pathField.text)
        }

        ActionButton {
            iconName: "arrowUp"
            size: "sm"
            variant: "ghost"
            enabled: root.machineId.length > 0 && root.currentPath !== "/"
                     && !backend.machineFilesBusy
            onClicked: root.go(root.parentPath(root.currentPath))
            Accessible.name: qsTr("Subir um diretório")
        }

        ActionButton {
            iconName: "refresh"
            size: "sm"
            variant: "ghost"
            enabled: root.machineId.length > 0 && !backend.machineFilesBusy
            onClicked: root.go(pathField.text)
            Accessible.name: qsTr("Atualizar listagem")
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 14
        Layout.rightMargin: 14
        spacing: 6

        PathField {
            id: uploadPath
            Layout.fillWidth: true
            title: qsTr("Enviar arquivo para a machine")
            filter: qsTr("Todos os arquivos (*)")
            placeholderText: qsTr("Arquivo local para enviar à machine…")
        }

        ActionButton {
            text: qsTr("Enviar")
            iconName: "upload"
            size: "sm"
            variant: "primary"
            enabled: root.machineId.length > 0 && uploadPath.text.length > 0
                     && !backend.machineFilesBusy
            onClicked: backend.uploadMachineFile(root.machineId, root.currentPath,
                                                 uploadPath.text)
        }
    }

    Text {
        Layout.fillWidth: true
        Layout.leftMargin: 14
        Layout.rightMargin: 14
        visible: backend.machineFilesBusy
        text: qsTr("Operação de arquivos em andamento…")
        color: Theme.textTertiary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontMicro
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.leftMargin: 14
        Layout.rightMargin: 14
        Layout.bottomMargin: 10
        color: Theme.bgSurface2
        radius: Theme.radiusSm
        border.width: 1
        border.color: Theme.border
        clip: true

        ListView {
            id: filesList
            anchors.fill: parent
            clip: true
            model: backend.machineFiles
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: fileRow
                width: filesList.width
                height: 34
                hoverEnabled: true
                enabled: !backend.machineFilesBusy

                onClicked: {
                    if (modelData.directory) {
                        root.go(modelData.path)
                        return
                    }
                    backend.downloadMachineFile(root.machineId, modelData.path,
                                                modelData.name)
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
                        visible: modelData.modified !== undefined
                                 && String(modelData.modified).length > 0
                        text: modelData.directory ? "" : String(modelData.modified)
                        color: Theme.textTertiary
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontMicro
                    }

                    Text {
                        text: modelData.directory ? qsTr("dir")
                                                  : root.formatSize(modelData.size)
                        color: Theme.textTertiary
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontMeta
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 32
                horizontalAlignment: Text.AlignHCenter
                visible: filesList.count === 0
                text: backend.machineFilesBusy
                      ? qsTr("Carregando…")
                      : root.machineId.length === 0
                        ? qsTr("Selecione uma machine para listar os arquivos.")
                        : qsTr("Diretório vazio ou sem permissão de leitura.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
                wrapMode: Text.WordWrap
            }
        }
    }

    Text {
        Layout.fillWidth: true
        Layout.leftMargin: 14
        Layout.rightMargin: 14
        Layout.bottomMargin: 10
        visible: backend.machineFilesError.length > 0
        text: backend.machineFilesError
        color: Theme.danger
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontMicro
        wrapMode: Text.WordWrap
    }

    Connections {
        target: backend

        function onMachineFilesChanged() {
            pathField.text = backend.machineFilesPath
        }
    }

    Component.onCompleted: pathField.text = backend.machineFilesPath
}
