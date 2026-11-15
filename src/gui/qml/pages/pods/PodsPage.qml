import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

ColumnLayout {
    id: podsView


    spacing: 0

    SelectionState { id: podSelection }

    readonly property var currentPod: podsList.currentRow >= 0
                                     ? backend.pods.items[podsList.currentRow] : null
    readonly property string currentPodId: currentPod ? String(currentPod.id) : ""
    /// Ação em andamento neste pod; desabilita só os botões dele.
    readonly property bool currentPodBusy: currentPodId.length > 0
                                           && backend.pods.runningTargets[currentPodId] !== undefined

    AppViewHeader {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.topMargin: 20
        Layout.bottomMargin: 16
        title: qsTr("Pods")
        subtitle: backend.pods.items.length + qsTr(" pods")

        AppButton {
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

        AppButton {
            visible: podSelection.count > 0
            text: qsTr("Start")
            iconName: "play"
            onClicked: backend.pods.batchPods(podSelection.ids(), "start")
        }

        AppButton {
            visible: podSelection.count > 0
            text: qsTr("Stop")
            iconName: "stop"
            onClicked: backend.pods.batchPods(podSelection.ids(), "stop")
        }

        AppButton {
            visible: podSelection.count > 0
            text: qsTr("Restart")
            onClicked: backend.pods.batchPods(podSelection.ids(), "restart")
        }

        AppButton {
            visible: podSelection.count > 0
            text: qsTr("Remover")
            variant: "danger"
            iconName: "trash"
            onClicked: batchRemoveDialog.open()
        }

        AppButton {
            visible: podSelection.count === 0
            text: qsTr("Start")
            busy: podsView.currentPodBusy
            busyText: qsTr("Iniciando…")
            enabled: podsView.currentPodId.length > 0 && !podsView.currentPodBusy
            onClicked: backend.pods.podAction("start", podsView.currentPodId)
        }

        AppButton {
            visible: podSelection.count === 0
            text: qsTr("Stop")
            busy: podsView.currentPodBusy
            busyText: qsTr("Parando…")
            enabled: podsView.currentPodId.length > 0 && !podsView.currentPodBusy
            onClicked: backend.pods.podAction("stop", podsView.currentPodId)
        }

        AppButton {
            visible: podSelection.count === 0
            text: qsTr("Restart")
            busy: podsView.currentPodBusy
            busyText: qsTr("Reiniciando…")
            enabled: podsView.currentPodId.length > 0 && !podsView.currentPodBusy
            onClicked: backend.pods.podAction("restart", podsView.currentPodId)
        }

        AppButton {
            visible: podSelection.count === 0
            text: qsTr("Remover")
            variant: "danger"
            enabled: podsView.currentPodId.length > 0 && !podsView.currentPodBusy
            onClicked: removeDialog.open()
        }

        AppButton {
            variant: "ghost"
            iconName: "refresh"
            onClicked: backend.pods.refresh()
            Accessible.name: qsTr("Atualizar")
        }
    }

    AppErrorBanner {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 8
        message: backend.pods.lastError
        actionText: qsTr("Dispensar")
        onActionClicked: backend.pods.dismissError()
    }

    AppCatalogList {
        id: podsList
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 20
        emptyIcon: "pods"
            emptyText: qsTr("Nenhum pod.\nCrie um pod para agrupar containers.")
        items: backend.pods.items
        screenState: backend.pods.loading ? "loading"
                     : backend.pods.items.length > 0 ? "ready"
                     : backend.pods.lastError.length > 0 ? "error" : "empty"
        errorText: backend.pods.lastError
        retrying: backend.pods.loading
        onRetryRequested: backend.pods.refresh()

        selection: podSelection
        idKey: "id"
        columns: [
            { key: "name", label: "NAME", width: 0 },
            { key: "status", label: "STATUS", width: 120 },
            { key: "containers", label: "CONTAINERS", width: 100 },
            { key: "id", label: "ID", width: 140, mono: true }
        ]
    }

    AppModal {
        id: createDialog
        title: qsTr("Novo pod")
        modal: true
        width: 440
        actionController: backend.pods
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: qsTr("Nome")
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            AppInput {
                id: podName
                Layout.fillWidth: true
                placeholderText: "app"
            }
        }
        footer: AppFooter {
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                enabled: !createDialog.busy
                onClicked: createDialog.close()
            }
            AppButton {
                text: qsTr("Criar")
                busy: createDialog.busy
                busyText: qsTr("Criando…")
                enabled: !createDialog.busy && podName.text.length > 0
                onClicked: createDialog.runAction(function() {
                    backend.pods.createPod(podName.text)
                }, "pods.create")
            }
        }
    }

    AppConfirmDialog {
        id: removeDialog
        title: qsTr("Remover pod?")
        message: qsTr("O pod e seus containers serão removidos.")
        confirmText: qsTr("Remover")
        destructive: true
        onConfirmed: {
            if (podsList.currentRow >= 0) {
                backend.pods.removePod(backend.pods.items[podsList.currentRow].id, true)
            }
        }
    }

    AppConfirmDialog {
        id: batchRemoveDialog
        title: qsTr("Remover pods selecionados")
        message: qsTr("%1 pod(s) serão removidos à força, junto com os containers.")
                     .arg(podSelection.count)
        confirmText: qsTr("Remover")
        destructive: true
        busyText: qsTr("Removendo…")
        actionController: backend.pods
        actionMethod: "pods.batch"
        onConfirmed: backend.pods.batchPods(podSelection.ids(), "remove", true)
    }
}
