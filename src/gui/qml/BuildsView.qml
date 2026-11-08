import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: buildsView

    spacing: 0

    property bool compact: false

    ViewHeader {
        Layout.fillWidth: true
        Layout.leftMargin: compact ? 14 : 22
        Layout.rightMargin: compact ? 14 : 22
        Layout.topMargin: 20
        Layout.bottomMargin: 16
        title: qsTr("Builds")
        subtitle: backend.builds.length + qsTr(" registros")

        Text {
            visible: backend.buildStatus.length > 0
            text: backend.buildStatus
            color: backend.buildActive ? Theme.cyan : Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            elide: Text.ElideRight
            Layout.maximumWidth: 260
        }

        ActionButton {
            text: qsTr("Novo build")
            variant: "primary"
            iconName: "plus"
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
            text: backend.buildActive ? qsTr("Cancelar") : qsTr("Remover")
            variant: backend.buildActive ? "secondary" : "danger"
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
            variant: "ghost"
            iconName: "refresh"
            onClicked: backend.refreshBuilds()
            Accessible.name: qsTr("Atualizar")
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.leftMargin: compact ? 14 : 22
        Layout.rightMargin: compact ? 14 : 22
        Layout.bottomMargin: 20
        spacing: compact ? 12 : 16

        CatalogList {
            id: buildsList
            Layout.preferredWidth: compact ? 300 : 460
            Layout.minimumWidth: 280
            Layout.fillHeight: true
            emptyIcon: "builds"
            emptyText: qsTr("Nenhum build registrado.\n"
                            + "Inicie um build informando contexto, Dockerfile e tag.")
            items: backend.builds
            columns: [
                { key: "imageRef", label: "IMAGEM", width: 0 },
                { key: "status", label: "STATUS", width: compact ? 54 : 80 },
                { key: "duration", label: "DURAÇÃO", width: compact ? 52 : 70 },
                { key: "trigger", label: "ORIGEM", width: compact ? 50 : 60 }
            ]
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.minimumWidth: compact ? 250 : 320
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
                    Layout.preferredHeight: 44
                    Layout.leftMargin: 14
                    Layout.rightMargin: 14
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: backend.activeBuildId.length > 0
                              ? backend.activeBuildId : qsTr("Log do build")
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                        elide: Text.ElideMiddle
                    }

                    AppBadge {
                        visible: backend.buildActive
                        text: qsTr("em execução")
                        tone: "accent"
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.border
                }

                ListView {
                    id: buildLogView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: backend.buildLog
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
                        function onBuildLogChanged() {
                            buildLogView.positionViewAtEnd()
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 40
                        horizontalAlignment: Text.AlignHCenter
                        visible: buildLogView.count === 0
                        text: qsTr("Inicie um build informando contexto, Dockerfile e tag.")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMeta
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    AppDialog {
        id: buildDialog
        title: qsTr("Novo build")
        modal: true
        width: 560
        anchors.centerIn: parent
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8

            GridLayout {
                columns: 2
                Layout.fillWidth: true
                columnSpacing: 10
                rowSpacing: 6

                Text { text: qsTr("Contexto"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: buildContext
                    Layout.fillWidth: true
                    placeholderText: "/home/usuario/projeto"
                }

                Text { text: qsTr("Dockerfile"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: buildDockerfile
                    Layout.fillWidth: true
                    placeholderText: "Dockerfile"
                }

                Text { text: qsTr("Tag"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: buildTag
                    Layout.fillWidth: true
                    placeholderText: "minha-imagem:latest"
                }

                Text { text: qsTr("Build args"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: buildArgs
                    Layout.fillWidth: true
                    placeholderText: "A=B, C=D"
                }

                Text { text: qsTr("Plataforma"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: buildPlatform
                    Layout.fillWidth: true
                    placeholderText: qsTr("linux/amd64 (opcional)")
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("O contexto é empacotado em tar (até 512 MB) e enviado via Libpod.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }
        footer: AppFooter {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                onClicked: buildDialog.close()
            }
            ActionButton {
                text: qsTr("Buildar")
                variant: "primary"
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
