import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: podsView


    spacing: 0

    SelectionState { id: podSelection }

    ViewHeader {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.topMargin: 20
        Layout.bottomMargin: 16
        title: qsTr("Pods")
        subtitle: backend.pods.items.length + qsTr(" pods")

        ActionButton {
            text: qsTr("Criar")
            variant: "primary"
            iconName: "plus"
            onClicked: {
                podName.text = ""
                createDialog.open()
            }
        }

        Text {
            visible: podSelection.count > 0
            text: podSelection.count + qsTr(" selecionados")
            color: Theme.indigo400
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMeta
        }

        ActionButton {
            visible: podSelection.count > 0
            text: qsTr("Start")
            iconName: "play"
            onClicked: backend.pods.batchPods(podSelection.ids(), "start")
        }

        ActionButton {
            visible: podSelection.count > 0
            text: qsTr("Stop")
            iconName: "stop"
            onClicked: backend.pods.batchPods(podSelection.ids(), "stop")
        }

        ActionButton {
            visible: podSelection.count > 0
            text: qsTr("Restart")
            onClicked: backend.pods.batchPods(podSelection.ids(), "restart")
        }

        ActionButton {
            visible: podSelection.count > 0
            text: qsTr("Remover")
            variant: "danger"
            iconName: "trash"
            onClicked: backend.pods.batchPods(podSelection.ids(), "remove", true)
        }

        ActionButton {
            visible: podSelection.count === 0
            text: qsTr("Start")
            enabled: podsList.currentRow >= 0
            onClicked: backend.pods.podAction("start", backend.pods.items[podsList.currentRow].id)
        }

        ActionButton {
            visible: podSelection.count === 0
            text: qsTr("Stop")
            enabled: podsList.currentRow >= 0
            onClicked: backend.pods.podAction("stop", backend.pods.items[podsList.currentRow].id)
        }

        ActionButton {
            visible: podSelection.count === 0
            text: qsTr("Restart")
            enabled: podsList.currentRow >= 0
            onClicked: backend.pods.podAction("restart", backend.pods.items[podsList.currentRow].id)
        }

        ActionButton {
            visible: podSelection.count === 0
            text: qsTr("Remover")
            variant: "danger"
            enabled: podsList.currentRow >= 0
            onClicked: removeDialog.open()
        }

        ActionButton {
            variant: "ghost"
            iconName: "refresh"
            onClicked: backend.refreshPods()
            Accessible.name: qsTr("Atualizar")
        }
    }

    CatalogList {
        id: podsList
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 20
        emptyIcon: "pods"
            emptyText: qsTr("Nenhum pod.\nCrie um pod para agrupar containers.")
        items: backend.pods.items
        selection: podSelection
        idKey: "id"
        columns: [
            { key: "name", label: "NAME", width: 0 },
            { key: "status", label: "STATUS", width: 120 },
            { key: "containers", label: "CONTAINERS", width: 100 },
            { key: "id", label: "ID", width: 140, mono: true }
        ]
    }

    AppDialog {
        id: createDialog
        title: "Novo pod"
        modal: true
        width: 440
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: "Nome"
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            FormField {
                id: podName
                Layout.fillWidth: true
                placeholderText: "app"
            }
        }
        footer: AppFooter {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: "Cancelar"
                variant: "secondary"
                onClicked: createDialog.close()
            }
            ActionButton {
                text: "Criar"
                enabled: podName.text.length > 0
                onClicked: {
                    backend.pods.createPod(podName.text)
                    createDialog.close()
                }
            }
        }
    }

    AppDialog {
        id: removeDialog
        title: "Remover pod?"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.color: Theme.border
        }
        contentItem: Label {
            text: "O pod e seus containers serão removidos."
            color: Theme.textPrimary
            wrapMode: Text.WordWrap
        }
        onAccepted: {
            if (podsList.currentRow >= 0) {
                backend.pods.removePod(backend.pods.items[podsList.currentRow].id, true)
            }
        }
    }
}
