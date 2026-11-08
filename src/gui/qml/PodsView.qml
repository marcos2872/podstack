import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: podsView

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
                text: "Pods"
                color: theme.textPrimary
                font.pixelSize: 15
                font.bold: true
            }

            Label {
                text: backend.pods.length + " itens"
                color: theme.textSecondary
                font.pixelSize: 11
            }

            Item { Layout.fillWidth: true }

            ActionButton {
                text: "Criar"
                onClicked: {
                    podName.text = ""
                    createDialog.open()
                }
            }

            ActionButton {
                text: "Start"
                accentColor: theme.success
                enabled: podsList.currentRow >= 0
                onClicked: backend.podAction("start", backend.pods[podsList.currentRow].id)
            }

            ActionButton {
                text: "Stop"
                accentColor: theme.warning
                enabled: podsList.currentRow >= 0
                onClicked: backend.podAction("stop", backend.pods[podsList.currentRow].id)
            }

            ActionButton {
                text: "Restart"
                accentColor: theme.indigo500
                enabled: podsList.currentRow >= 0
                onClicked: backend.podAction("restart", backend.pods[podsList.currentRow].id)
            }

            ActionButton {
                text: "Remover"
                accentColor: theme.danger
                enabled: podsList.currentRow >= 0
                onClicked: removeDialog.open()
            }

            ActionButton {
                text: "Atualizar"
                accentColor: theme.bgElevated
                onClicked: backend.refreshPods()
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: theme.border
    }

    CatalogList {
        id: podsList
        Layout.fillWidth: true
        Layout.fillHeight: true
        items: backend.pods
        columns: [
            { key: "name", label: "NAME", width: 0 },
            { key: "status", label: "STATUS", width: 120 },
            { key: "containers", label: "CONTAINERS", width: 100 },
            { key: "id", label: "ID", width: 140, mono: true }
        ]
    }

    Label {
        Layout.fillWidth: true
        Layout.margins: 24
        visible: backend.pods.length === 0
        horizontalAlignment: Text.AlignHCenter
        text: "Nenhum pod.\nCrie um pod para agrupar containers."
        color: theme.textSecondary
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }

    Dialog {
        id: createDialog
        title: "Novo pod"
        modal: true
        width: 440
        background: Rectangle {
            color: theme.bgSurface
            radius: 10
            border.color: theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: "Nome"
                color: theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            FormField {
                id: podName
                Layout.fillWidth: true
                placeholderText: "app"
            }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: "Cancelar"
                accentColor: theme.bgElevated
                onClicked: createDialog.close()
            }
            ActionButton {
                text: "Criar"
                enabled: podName.text.length > 0
                onClicked: {
                    backend.createPod(podName.text)
                    createDialog.close()
                }
            }
        }
    }

    Dialog {
        id: removeDialog
        title: "Remover pod?"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle {
            color: theme.bgSurface
            radius: 10
            border.color: theme.border
        }
        contentItem: Label {
            text: "O pod e seus containers serão removidos."
            color: theme.textPrimary
            wrapMode: Text.WordWrap
        }
        onAccepted: {
            if (podsList.currentRow >= 0) {
                backend.removePod(backend.pods[podsList.currentRow].id, true)
            }
        }
    }
}
