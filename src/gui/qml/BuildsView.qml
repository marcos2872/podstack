import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: buildsView

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
                text: "Builds"
                color: theme.textPrimary
                font.pixelSize: 15
                font.bold: true
            }

            Label {
                text: backend.builds.length + " registros"
                color: theme.textSecondary
                font.pixelSize: 11
            }

            Item { Layout.fillWidth: true }

            Label {
                text: backend.buildStatus
                visible: backend.buildStatus.length > 0
                color: backend.buildActive ? theme.cyan : theme.textSecondary
                font.pixelSize: 10
                elide: Text.ElideRight
                Layout.maximumWidth: 260
            }

            ActionButton {
                text: "Novo build"
                onClicked: {
                    buildContext.text = ""
                    buildDockerfile.text = "Dockerfile"
                    buildTag.text = ""
                    buildArgs.text = ""
                    buildPlatform.text = ""
                    buildDialog.open()
                }
            }

            ActionButton {
                text: backend.buildActive ? "Cancelar" : "Remover"
                accentColor: backend.buildActive ? theme.warning : theme.danger
                enabled: backend.buildActive || buildsList.currentRow >= 0
                onClicked: {
                    if (backend.buildActive) {
                        backend.cancelBuild()
                    } else if (buildsList.currentRow >= 0) {
                        backend.removeBuild(backend.builds[buildsList.currentRow].id)
                    }
                }
            }

            ActionButton {
                text: "Atualizar"
                accentColor: theme.bgElevated
                onClicked: backend.refreshBuilds()
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

        CatalogList {
            id: buildsList
            SplitView.preferredWidth: 460
            SplitView.minimumWidth: 340
            items: backend.builds
            columns: [
                { key: "imageRef", label: "IMAGEM", width: 0 },
                { key: "status", label: "STATUS", width: 80 },
                { key: "duration", label: "DURAÇÃO", width: 70 },
                { key: "trigger", label: "ORIGEM", width: 60 }
            ]
        }

        ColumnLayout {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 320
            spacing: 6
            Layout.margins: 8

            RowLayout {
                Layout.fillWidth: true

                Label {
                    Layout.fillWidth: true
                    text: backend.activeBuildId.length > 0
                          ? backend.activeBuildId : "Log do build"
                    color: theme.textPrimary
                    font.pixelSize: 12
                    font.bold: true
                    elide: Text.ElideMiddle
                }

                Label {
                    text: backend.buildActive ? "em execução" : ""
                    color: theme.cyan
                    font.pixelSize: 10
                }
            }

            ListView {
                id: buildLogView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: backend.buildLog
                ScrollBar.vertical: ScrollBar {}

                delegate: Text {
                    width: buildLogView.width
                    text: modelData
                    color: theme.textSecondary
                    font.family: "monospace"
                    font.pixelSize: 10
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                }

                Connections {
                    target: backend
                    function onBuildLogChanged() {
                        buildLogView.positionViewAtEnd()
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: backend.buildLog.length === 0
                text: "Inicie um build informando contexto, Dockerfile e tag."
                color: theme.textSecondary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
        }
    }

    Dialog {
        id: buildDialog
        title: "Novo build"
        modal: true
        width: 560
        background: Rectangle {
            color: theme.bgSurface
            radius: 10
            border.color: theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            GridLayout {
                columns: 2
                Layout.fillWidth: true
                columnSpacing: 10
                rowSpacing: 6

                Label { text: "Contexto"; color: theme.textSecondary; font.pixelSize: 11 }
                FormField {
                    id: buildContext
                    Layout.fillWidth: true
                    placeholderText: "/home/usuario/projeto"
                }

                Label { text: "Dockerfile"; color: theme.textSecondary; font.pixelSize: 11 }
                FormField {
                    id: buildDockerfile
                    Layout.fillWidth: true
                    placeholderText: "Dockerfile"
                }

                Label { text: "Tag"; color: theme.textSecondary; font.pixelSize: 11 }
                FormField {
                    id: buildTag
                    Layout.fillWidth: true
                    placeholderText: "minha-imagem:latest"
                }

                Label { text: "Build args"; color: theme.textSecondary; font.pixelSize: 11 }
                FormField {
                    id: buildArgs
                    Layout.fillWidth: true
                    placeholderText: "A=B, C=D"
                }

                Label { text: "Plataforma"; color: theme.textSecondary; font.pixelSize: 11 }
                FormField {
                    id: buildPlatform
                    Layout.fillWidth: true
                    placeholderText: "linux/amd64 (opcional)"
                }
            }

            Label {
                Layout.fillWidth: true
                text: "O contexto é empacotado em tar (até 512 MB) e enviado via Libpod."
                color: theme.textSecondary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: "Cancelar"
                accentColor: theme.bgElevated
                onClicked: buildDialog.close()
            }
            ActionButton {
                text: "Buildar"
                enabled: buildContext.text.length > 0 && buildTag.text.length > 0
                         && !backend.buildActive
                onClicked: {
                    backend.startBuild({
                        contextPath: buildContext.text,
                        dockerfile: buildDockerfile.text,
                        tag: buildTag.text,
                        buildArgsText: buildArgs.text,
                        platform: buildPlatform.text
                    })
                    buildDialog.close()
                }
            }
        }
    }

    Component.onCompleted: backend.refreshBuilds()
}
