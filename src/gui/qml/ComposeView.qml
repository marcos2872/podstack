import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: composeView

    spacing: 0

    ViewHeader {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.topMargin: 20
        Layout.bottomMargin: 16
        title: qsTr("Compose")
        subtitle: backend.composeProjects.length + qsTr(" projetos")

        ActionButton {
            text: qsTr("Up por diretório")
            variant: "primary"
            iconName: "play"
            onClicked: {
                upProjectPath.text = ""
                upDialog.open()
            }
        }

        ActionButton {
            variant: "ghost"
            iconName: "refresh"
            onClicked: backend.refreshCompose()
            Accessible.name: qsTr("Atualizar")
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 14
        spacing: 7

        ActionButton {
            text: qsTr("Up")
            enabled: composeList.currentRow >= 0
            onClicked: backend.composeUp(
                           backend.composeProjects[composeList.currentRow].workingDir)
        }

        ActionButton {
            text: qsTr("Down")
            enabled: composeList.currentRow >= 0
            onClicked: downDialog.open()
        }

        ActionButton {
            text: qsTr("Restart")
            enabled: composeList.currentRow >= 0
            onClicked: backend.composeRestart(
                           backend.composeProjects[composeList.currentRow].workingDir, "")
        }

        ActionButton {
            text: qsTr("Abrir YAML")
            iconName: "file"
            enabled: composeList.currentRow >= 0
            onClicked: {
                const project = backend.composeProjects[composeList.currentRow]
                backend.openPath(project.configFile.length > 0
                                 ? project.configFile : project.workingDir)
            }
        }

        ActionButton {
            text: qsTr("Abrir diretório")
            iconName: "folder"
            enabled: composeList.currentRow >= 0
            onClicked: backend.openPath(
                           backend.composeProjects[composeList.currentRow].workingDir)
        }

        Item { Layout.fillWidth: true }
    }

    SplitView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 20
        spacing: 16

        CatalogList {
            id: composeList
            SplitView.preferredWidth: 480
            SplitView.minimumWidth: 340
            emptyIcon: "compose"
            emptyText: qsTr("Nenhum projeto Compose em execução.\n"
                            + "Use a CLI: podstack compose up <diretório>.")
            items: backend.composeProjects
            columns: [
                { key: "name", label: "PROJETO", width: 0 },
                { key: "status", label: "STATUS", width: 80 },
                { key: "services", label: "SERVIÇOS", width: 150 }
            ]
        }

        Rectangle {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 320
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.border
            clip: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    spacing: 8

                    Text {
                        text: qsTr("Logs")
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                    }

                    FormField {
                        id: logService
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        placeholderText: qsTr("serviço (vazio = todos)")
                    }

                    ActionButton {
                        text: qsTr("Assinar")
                        size: "sm"
                        enabled: composeList.currentRow >= 0
                        onClicked: backend.subscribeComposeLogs(
                                       backend.composeProjects[composeList.currentRow].name,
                                       logService.text)
                    }

                    ActionButton {
                        text: qsTr("Parar")
                        variant: "ghost"
                        size: "sm"
                        onClicked: backend.unsubscribeComposeLogs()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.border
                }

                ListView {
                    id: composeLogs
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: backend.composeLogLines
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Text {
                        width: ListView.view.width
                        text: modelData
                        color: Theme.textSecondary
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontMeta
                        textFormat: Text.PlainText
                        wrapMode: Text.WrapAnywhere
                        padding: 10
                    }

                    Connections {
                        target: backend
                        function onComposeLogsChanged() {
                            composeLogs.positionViewAtEnd()
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 40
                        horizontalAlignment: Text.AlignHCenter
                        visible: composeLogs.count === 0
                        text: qsTr("Selecione um projeto e assine os logs por serviço ou de todos.")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMeta
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    Dialog {
        id: upDialog
        title: qsTr("Subir projeto Compose")
        modal: true
        width: 460
        anchors.centerIn: parent
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8

            Text {
                text: qsTr("Diretório do projeto")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
                font.weight: Font.DemiBold
            }

            FormField {
                id: upProjectPath
                Layout.fillWidth: true
                placeholderText: "/home/usuario/projeto"
            }

            Text {
                text: qsTr("O Compose procura compose.yaml ou docker-compose.yml no diretório.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                onClicked: upDialog.close()
            }
            ActionButton {
                text: qsTr("Subir")
                variant: "primary"
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
        title: qsTr("Derrubar projeto?")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8

            Text {
                text: qsTr("Os containers e a rede do projeto serão removidos. "
                           + "Volumes são preservados.")
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                wrapMode: Text.WordWrap
            }

            FormCheck {
                id: removeVolumes
                text: qsTr("Remover volumes também")
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
