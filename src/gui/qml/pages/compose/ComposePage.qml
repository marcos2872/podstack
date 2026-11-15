import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

ColumnLayout {
    id: composeView

    spacing: 0

    property bool compact: false

    SelectionState { id: composeSelection }

    readonly property var selectedProject: composeList.currentRow >= 0
                                          ? backend.compose.items[composeList.currentRow] : null
    readonly property string selectedPath: selectedProject ? String(selectedProject.workingDir) : ""
    /// Ação em andamento neste projeto (up/down/restart/stop/start).
    readonly property bool selectedBusy: selectedPath.length > 0
                                         && backend.compose.runningTargets[selectedPath] !== undefined
    /// O log segue o fim só enquanto o usuário não rolar para cima.
    property bool followLogs: true

    function openProject(project, service, logs) {
        for (let i = 0; i < backend.compose.items.length; ++i) {
            if (backend.compose.items[i].name === project) {
                composeList.currentRow = i
                break
            }
        }
        if (logs) {
            backend.compose.subscribeComposeLogs(project, service || "")
        }
    }

    AppViewHeader {
        Layout.fillWidth: true
        Layout.leftMargin: compact ? 14 : 22
        Layout.rightMargin: compact ? 20 : 22
        Layout.topMargin: 20
        Layout.bottomMargin: 16
        title: qsTr("Compose")
        subtitle: backend.compose.items.length + qsTr(" projetos")

        AppButton {
            text: qsTr("Up por diretório")
            variant: "primary"
            iconName: "play"
            onClicked: {
                upProjectPath.text = ""
                upDialog.open()
            }
        }

        AppButton {
            variant: "ghost"
            iconName: "refresh"
            onClicked: backend.compose.refresh()
            Accessible.name: qsTr("Atualizar")
        }
    }

    AppErrorBanner {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 8
        message: backend.compose.lastError
        actionText: qsTr("Dispensar")
        onActionClicked: backend.compose.dismissError()
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: compact ? 14 : 22
        Layout.rightMargin: compact ? 20 : 22
        Layout.bottomMargin: 14
        spacing: 7

        Text {
            visible: composeSelection.count > 0
            text: composeSelection.count + qsTr(" selecionados")
            color: Theme.indigo400
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMeta
        }

        AppButton {
            visible: composeSelection.count > 0
            text: qsTr("Up")
            iconName: "play"
            enabled: !backend.compose.actionActive
            onClicked: backend.compose.batchCompose(composeSelection.ids(), "up")
        }

        AppButton {
            visible: composeSelection.count > 0
            text: qsTr("Down")
            iconName: "stop"
            enabled: !backend.compose.actionActive
            onClicked: batchDownDialog.open()
        }

        AppButton {
            visible: composeSelection.count > 0
            text: qsTr("Restart")
            enabled: !backend.compose.actionActive
            onClicked: backend.compose.batchCompose(composeSelection.ids(), "restart")
        }

        AppButton {
            visible: composeSelection.count === 0
            text: qsTr("Up")
            busy: composeView.selectedBusy
            busyText: qsTr("Subindo…")
            enabled: composeView.selectedPath.length > 0 && !composeView.selectedBusy
            onClicked: backend.compose.composeUp(composeView.selectedPath)
        }

        AppButton {
            visible: composeSelection.count === 0
            text: qsTr("Down")
            busy: composeView.selectedBusy
            busyText: qsTr("Derrubando…")
            enabled: composeView.selectedPath.length > 0 && !composeView.selectedBusy
            onClicked: downDialog.open()
        }

        AppButton {
            visible: composeSelection.count === 0
            text: qsTr("Restart")
            busy: composeView.selectedBusy
            busyText: qsTr("Reiniciando…")
            enabled: composeView.selectedPath.length > 0 && !composeView.selectedBusy
            onClicked: backend.compose.composeRestart(composeView.selectedPath, "")
        }

        AppButton {
            text: qsTr("Abrir YAML")
            iconName: "file"
            enabled: composeView.selectedProject !== null
            onClicked: {
                const project = composeView.selectedProject
                backend.openPath(project.configFile.length > 0
                                 ? project.configFile : project.workingDir)
            }
        }

        AppButton {
            text: qsTr("Abrir diretório")
            iconName: "folder"
            enabled: composeView.selectedProject !== null
            onClicked: backend.openPath(composeView.selectedPath)
        }

        Item { Layout.fillWidth: true }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.leftMargin: compact ? 14 : 22
        Layout.rightMargin: compact ? 20 : 22
        Layout.bottomMargin: 20
        spacing: compact ? 12 : 16

        AppCatalogList {
            id: composeList
            Layout.preferredWidth: compact ? 280 : 480
            Layout.minimumWidth: 280
            Layout.fillHeight: true
            emptyIcon: "compose"
            emptyText: qsTr("Nenhum projeto Compose em execução.\n"
                            + "Use a CLI: podstack compose up <diretório>.")
            items: backend.compose.items
            selection: composeSelection
            idKey: "workingDir"
            columns: [
                { key: "name", label: "PROJETO", width: 0 },
                { key: "status", label: "STATUS", width: compact ? 58 : 80 },
                { key: "services", label: "SERVIÇOS", width: compact ? 90 : 150 }
            ]
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.minimumWidth: compact ? 240 : 320
            Layout.fillHeight: true
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

                    AppInput {
                        id: logService
                        Layout.fillWidth: true
                        Layout.minimumWidth: 60
                        Layout.preferredHeight: 30
                        placeholderText: qsTr("serviço (vazio = todos)")
                    }

                    AppButton {
                        text: qsTr("Assinar")
                        size: "sm"
                        enabled: composeView.selectedProject !== null
                        onClicked: backend.compose.subscribeComposeLogs(
                                       String(composeView.selectedProject.name), logService.text)
                    }

                    AppButton {
                        text: qsTr("Parar")
                        variant: "ghost"
                        size: "sm"
                        onClicked: backend.compose.unsubscribeComposeLogs()
                    }

                    AppButton {
                        text: composeView.followLogs ? qsTr("Seguindo") : qsTr("Seguir")
                        variant: composeView.followLogs ? "ghost" : "secondary"
                        size: "sm"
                        onClicked: {
                            composeView.followLogs = true
                            composeLogs.positionViewAtEnd()
                        }
                        Accessible.name: qsTr("Seguir o fim do log")
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
                    model: backend.compose.logLines
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
                        target: backend.compose
                        function onLogLinesChanged() {
                            if (composeView.followLogs) {
                                composeLogs.positionViewAtEnd()
                            }
                        }
                    }

                    // Rolar para cima pausa o "seguir"; voltar ao fim religa.
                    onMovementEnded: composeView.followLogs = atYEnd

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

    AppModal {
        id: upDialog
        title: qsTr("Subir projeto Compose")
        modal: true
        width: 460
        anchors.centerIn: parent
        actionController: backend.compose
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

            AppPathField {
                id: upProjectPath
                Layout.fillWidth: true
                title: qsTr("Diretório do projeto")
                mode: "directory"
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
        footer: AppFooter {
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                enabled: !upDialog.busy
                onClicked: upDialog.close()
            }
            AppButton {
                text: qsTr("Subir")
                variant: "primary"
                busy: upDialog.busy
                busyText: qsTr("Subindo…")
                enabled: !upDialog.busy && upProjectPath.text.length > 0
                onClicked: upDialog.runAction(function() {
                    backend.compose.composeUp(upProjectPath.text)
                }, "compose.up")
            }
        }
    }

    AppModal {
        id: downDialog
        title: qsTr("Derrubar projeto?")
        modal: true
        anchors.centerIn: parent
        actionController: backend.compose
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

            AppCheck {
                id: removeVolumes
                text: qsTr("Remover volumes também")
            }
        }
        onOpened: removeVolumes.checked = false
        footer: AppFooter {
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                enabled: !downDialog.busy
                onClicked: downDialog.close()
            }
            AppButton {
                text: qsTr("Derrubar")
                variant: "danger"
                busy: downDialog.busy
                busyText: qsTr("Derrubando…")
                enabled: !downDialog.busy && composeView.selectedPath.length > 0
                onClicked: downDialog.runAction(function() {
                    backend.compose.composeDown(composeView.selectedPath, removeVolumes.checked)
                }, "compose.down")
            }
        }
    }

    Component.onCompleted: backend.compose.refresh()
    Component.onDestruction: backend.compose.unsubscribeComposeLogs()

    // Sair da página encerra o stream: antes ele ficava crescendo em background.
    onVisibleChanged: {
        if (!visible) {
            backend.compose.unsubscribeComposeLogs()
            composeView.followLogs = true
        }
    }

    AppConfirmDialog {
        id: batchDownDialog
        title: qsTr("Derrubar projetos selecionados")
        message: qsTr("%1 projeto(s) serão derrubados. Os volumes são preservados.")
                     .arg(composeSelection.count)
        confirmText: qsTr("Derrubar")
        destructive: true
        busyText: qsTr("Derrubando…")
        actionController: backend.compose
        actionMethod: "compose.batch"
        onConfirmed: backend.compose.batchCompose(composeSelection.ids(), "down")
    }
}
