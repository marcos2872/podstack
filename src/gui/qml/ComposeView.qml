import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: composeView

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
                text: "Compose"
                color: theme.textPrimary
                font.pixelSize: 15
                font.bold: true
            }

            Label {
                text: backend.composeProjects.length + " projetos"
                color: theme.textSecondary
                font.pixelSize: 11
            }

            Item { Layout.fillWidth: true }

            ActionButton {
                text: "Up"
                enabled: composeList.currentRow >= 0
                onClicked: backend.composeUp(backend.composeProjects[composeList.currentRow].workingDir)
            }

            ActionButton {
                text: "Up por diretório"
                onClicked: {
                    upProjectPath.text = ""
                    upDialog.open()
                }
            }

            ActionButton {
                text: "Down"
                accentColor: theme.warning
                enabled: composeList.currentRow >= 0
                onClicked: downDialog.open()
            }

            ActionButton {
                text: "Restart"
                accentColor: theme.indigo500
                enabled: composeList.currentRow >= 0
                onClicked: backend.composeRestart(
                               backend.composeProjects[composeList.currentRow].workingDir, "")
            }

            ActionButton {
                text: "Abrir YAML"
                accentColor: theme.bgElevated
                enabled: composeList.currentRow >= 0
                onClicked: {
                    const project = backend.composeProjects[composeList.currentRow]
                    backend.openPath(project.configFile.length > 0
                                     ? project.configFile : project.workingDir)
                }
            }

            ActionButton {
                text: "Abrir diretório"
                accentColor: theme.bgElevated
                enabled: composeList.currentRow >= 0
                onClicked: backend.openPath(
                               backend.composeProjects[composeList.currentRow].workingDir)
            }

            ActionButton {
                text: "Atualizar"
                accentColor: theme.bgElevated
                onClicked: backend.refreshCompose()
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: theme.border
    }

    SplitView {
        Layout.fillWidth: true
        Layout.fillHeight: true

        ColumnLayout {
            SplitView.preferredWidth: 420
            SplitView.minimumWidth: 320
            spacing: 0

            CatalogList {
                id: composeList
                Layout.fillWidth: true
                Layout.fillHeight: true
                items: backend.composeProjects
                columns: [
                    { key: "name", label: "PROJETO", width: 0 },
                    { key: "status", label: "STATUS", width: 80 },
                    { key: "services", label: "SERVIÇOS", width: 150 }
                ]
            }

            Label {
                Layout.fillWidth: true
                Layout.margins: 16
                visible: backend.composeProjects.length === 0
                text: "Nenhum projeto Compose em execução.\nUse a CLI: podstack compose up <diretório>."
                color: theme.textSecondary
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }

        ColumnLayout {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 320
            spacing: 6
            Layout.margins: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Label {
                    text: "Logs"
                    color: theme.textPrimary
                    font.pixelSize: 12
                    font.bold: true
                }

                FormField {
                    id: logService
                    Layout.fillWidth: true
                    placeholderText: "serviço (vazio = todos)"
                }

                ActionButton {
                    text: "Assinar"
                    enabled: composeList.currentRow >= 0
                    onClicked: backend.subscribeComposeLogs(
                                   backend.composeProjects[composeList.currentRow].name,
                                   logService.text)
                }

                ActionButton {
                    text: "Parar"
                    accentColor: theme.bgElevated
                    onClicked: backend.unsubscribeComposeLogs()
                }
            }

            ListView {
                id: composeLogs
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: backend.composeLogLines
                ScrollBar.vertical: ScrollBar {}

                delegate: Text {
                    width: composeLogs.width
                    text: modelData
                    color: theme.textSecondary
                    font.family: "monospace"
                    font.pixelSize: 10
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                }

                Connections {
                    target: backend
                    function onComposeLogsChanged() {
                        composeLogs.positionViewAtEnd()
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: backend.composeLogLines.length === 0
                text: "Selecione um projeto e assine os logs por serviço ou de todos."
                color: theme.textSecondary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
        }
    }

    Dialog {
        id: upDialog
        title: "Subir projeto Compose"
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
                text: "Diretório do projeto"
                color: theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            FormField {
                id: upProjectPath
                Layout.fillWidth: true
                placeholderText: "/home/usuario/projeto"
            }
            Label {
                text: "O Compose procura compose.yaml ou docker-compose.yml no diretório."
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
                onClicked: upDialog.close()
            }
            ActionButton {
                text: "Subir"
                enabled: upProjectPath.text.length > 0
                onClicked: {
                    backend.composeUp(upProjectPath.text)
                    upDialog.close()
                }
            }
        }
    }

    Dialog {
        id: downDialog
        title: "Derrubar projeto?"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle {
            color: theme.bgSurface
            radius: 10
            border.color: theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: "Os containers e a rede do projeto serão removidos. Volumes são preservados."
                color: theme.textPrimary
                wrapMode: Text.WordWrap
            }
            FormCheck {
                id: removeVolumes
                text: "Remover volumes também"
            }
        }
        onAccepted: {
            if (composeList.currentRow >= 0) {
                backend.composeDown(backend.composeProjects[composeList.currentRow].workingDir,
                                    removeVolumes.checked)
            }
        }
    }

    Component.onCompleted: backend.refreshCompose()
    Component.onDestruction: backend.unsubscribeComposeLogs()
}
