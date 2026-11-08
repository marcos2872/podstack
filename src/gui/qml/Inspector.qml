import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

Rectangle {
    id: inspector

    property var selected: null

    Theme { id: theme }

    color: theme.bgSurface

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        Label {
            text: inspector.selected ? inspector.selected.name : "Nenhum container selecionado"
            color: theme.textPrimary
            font.pixelSize: 16
            font.bold: true
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        Label {
            text: inspector.selected ? inspector.selected.image : ""
            color: theme.textSecondary
            font.pixelSize: 11
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        TabBar {
            id: inspectorTabs
            Layout.fillWidth: true
            background: Rectangle { color: "transparent" }
            onCurrentIndexChanged: inspector.syncStreams()

            TabButton {
                id: infoTab
                text: "Info"
                contentItem: Text {
                    text: infoTab.text
                    color: inspectorTabs.currentIndex === 0 ? theme.textPrimary
                                                            : theme.textSecondary
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            TabButton {
                id: logsTab
                text: "Logs"
                contentItem: Text {
                    text: logsTab.text
                    color: inspectorTabs.currentIndex === 1 ? theme.textPrimary
                                                            : theme.textSecondary
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            TabButton {
                id: statsTab
                text: "Stats"
                contentItem: Text {
                    text: statsTab.text
                    color: inspectorTabs.currentIndex === 2 ? theme.textPrimary
                                                            : theme.textSecondary
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            TabButton {
                id: filesTab
                text: "Files"
                contentItem: Text {
                    text: filesTab.text
                    color: inspectorTabs.currentIndex === 3 ? theme.textPrimary
                                                            : theme.textSecondary
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: inspectorTabs.currentIndex

            // Info
            ColumnLayout {
                spacing: 8

                Label {
                    text: "ID"
                    color: theme.textSecondary
                    font.pixelSize: 10
                    font.bold: true
                }

                Label {
                    text: inspector.selected ? inspector.selected.id : "—"
                    color: theme.textPrimary
                    font.pixelSize: 11
                    wrapMode: Text.WrapAnywhere
                    Layout.fillWidth: true
                }

                Label {
                    text: "PORTAS"
                    color: theme.textSecondary
                    font.pixelSize: 10
                    font.bold: true
                }

                Label {
                    text: inspector.selected && inspector.selected.ports.length > 0
                          ? inspector.selected.ports : "—"
                    color: theme.textPrimary
                    font.pixelSize: 11
                    wrapMode: Text.WrapAnywhere
                    Layout.fillWidth: true
                }

                Label {
                    text: "ESTADO"
                    color: theme.textSecondary
                    font.pixelSize: 10
                    font.bold: true
                }

                Label {
                    text: inspector.selected ? inspector.selected.state : "—"
                    color: theme.textPrimary
                    font.pixelSize: 11
                }

                Label {
                    text: "SAÚDE"
                    color: theme.textSecondary
                    font.pixelSize: 10
                    font.bold: true
                }

                Label {
                    text: inspector.selected && inspector.selected.health
                          ? inspector.selected.health : "—"
                    color: inspector.selected && inspector.selected.health === "unhealthy"
                           ? theme.danger
                           : inspector.selected && inspector.selected.health === "starting"
                             ? theme.warning : theme.textPrimary
                    font.pixelSize: 11
                }

                Label {
                    text: "AÇÕES"
                    color: theme.textSecondary
                    font.pixelSize: 10
                    font.bold: true
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 6
                    rowSpacing: 6

                    ActionButton {
                        Layout.fillWidth: true
                        text: "Iniciar"
                        enabled: inspector.selected !== null
                        onClicked: backend.lifecycle("start", inspector.selected.id)
                    }
                    ActionButton {
                        Layout.fillWidth: true
                        text: "Parar"
                        enabled: inspector.selected !== null
                        onClicked: backend.lifecycle("stop", inspector.selected.id)
                    }
                    ActionButton {
                        Layout.fillWidth: true
                        text: "Reiniciar"
                        enabled: inspector.selected !== null
                        onClicked: backend.lifecycle("restart", inspector.selected.id)
                    }
                    ActionButton {
                        Layout.fillWidth: true
                        text: "Pausar"
                        enabled: inspector.selected !== null
                        onClicked: backend.lifecycle("pause", inspector.selected.id)
                    }
                    ActionButton {
                        Layout.fillWidth: true
                        text: "Despausar"
                        enabled: inspector.selected !== null
                        onClicked: backend.lifecycle("unpause", inspector.selected.id)
                    }
                    ActionButton {
                        Layout.fillWidth: true
                        text: "Matar"
                        accentColor: theme.warning
                        enabled: inspector.selected !== null
                        onClicked: backend.lifecycle("kill", inspector.selected.id)
                    }
                    ActionButton {
                        Layout.fillWidth: true
                        text: "Terminal"
                        accentColor: theme.indigo700
                        enabled: inspector.selected !== null
                        onClicked: backend.openTerminal(inspector.selected.id)
                    }
                    ActionButton {
                        Layout.fillWidth: true
                        text: "Abrir porta"
                        accentColor: theme.indigo500
                        enabled: inspector.selected !== null
                                 && inspector.selected.ports.length > 0
                        onClicked: {
                            const match = inspector.selected.ports.match(/(\d+)->/)
                            if (match) {
                                Qt.openUrlExternally("http://localhost:" + match[1])
                            }
                        }
                    }
                    ActionButton {
                        Layout.fillWidth: true
                        text: "Renomear"
                        accentColor: theme.bgElevated
                        enabled: inspector.selected !== null
                        onClicked: {
                            renameDialog.containerId = inspector.selected.id
                            renameName.text = inspector.selected.name
                            renameDialog.open()
                        }
                    }
                    ActionButton {
                        Layout.fillWidth: true
                        text: "Duplicar"
                        accentColor: theme.indigo500
                        enabled: inspector.selected !== null
                        onClicked: backend.duplicateContainer(inspector.selected.id)
                    }
                    ActionButton {
                        Layout.fillWidth: true
                        text: "Remover"
                        accentColor: theme.danger
                        enabled: inspector.selected !== null
                        onClicked: {
                            removeDialog.containerId = inspector.selected.id
                            removeDialog.open()
                        }
                    }
                }

                Label {
                    text: backend.lastError
                    visible: backend.lastError.length > 0
                    color: theme.danger
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Item { Layout.fillHeight: true }
            }

            LogsView {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            StatsView {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            FilesView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                containerId: inspector.selected ? inspector.selected.id : ""
            }
        }
    }

    Dialog {
        id: removeDialog
        property string containerId
        title: "Remover container?"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle {
            color: theme.bgSurface
            radius: 10
            border.color: theme.border
        }
        contentItem: Label {
            text: "O container será removido permanentemente."
            color: theme.textPrimary
            wrapMode: Text.WordWrap
        }
        onAccepted: backend.removeContainer(containerId, true, false)
    }

    Dialog {
        id: renameDialog
        property string containerId
        title: "Renomear container"
        modal: true
        width: 420
        background: Rectangle {
            color: theme.bgSurface
            radius: 10
            border.color: theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: "Novo nome"
                color: theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            FormField {
                id: renameName
                Layout.fillWidth: true
            }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: "Cancelar"
                accentColor: theme.bgElevated
                onClicked: renameDialog.close()
            }
            ActionButton {
                text: "Salvar"
                onClicked: {
                    backend.renameContainer(renameDialog.containerId, renameName.text)
                    renameDialog.close()
                }
            }
        }
    }

    function syncStreams() {
        if (!selected) {
            backend.unsubscribeLogs()
            backend.unsubscribeStats()
            return
        }
        if (inspectorTabs.currentIndex === 1) {
            backend.unsubscribeStats()
            backend.subscribeLogs(selected.id, 200, true)
        } else if (inspectorTabs.currentIndex === 2) {
            backend.unsubscribeLogs()
            backend.subscribeStats(selected.id)
        } else if (inspectorTabs.currentIndex === 3) {
            backend.unsubscribeLogs()
            backend.unsubscribeStats()
            backend.listFiles(selected.id, "/")
        } else {
            backend.unsubscribeLogs()
            backend.unsubscribeStats()
        }
    }

    onSelectedChanged: syncStreams()
    Component.onCompleted: syncStreams()
    Component.onDestruction: {
        backend.unsubscribeLogs()
        backend.unsubscribeStats()
    }
}
