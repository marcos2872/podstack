import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: page

    property int tab: 0
    property string logsNamespace: ""
    property string logsPod: ""
    property string actionNamespace: ""
    property string actionName: ""
    property string actionKind: ""
    property string clusterActionKind: "disable"

    function selectedItem() {
        if (page.tab === 0) {
            return podsList.currentRow >= 0 ? backend.k8sPods[podsList.currentRow] : null
        }
        return servicesList.currentRow >= 0 ? backend.k8sServices[servicesList.currentRow] : null
    }

    function openDelete() {
        const item = page.selectedItem()
        if (!item) {
            return
        }
        actionNamespace = item.namespace
        actionName = item.name
        actionKind = page.tab === 0 ? "pod" : "service"
        deleteDialog.open()
    }

    function openPortForward() {
        const pod = backend.k8sPods[podsList.currentRow]
        if (!pod) {
            return
        }
        actionNamespace = pod.namespace
        actionName = pod.name
        forwardLocal.text = "8080"
        forwardRemote.text = "80"
        backend.stopK8sPortForward()
        forwardDialog.open()
    }

    function openClusterAction(kind) {
        clusterActionKind = kind
        clusterActionDialog.open()
    }

    function openLogs() {
        const pod = backend.k8sPods[podsList.currentRow]
        if (!pod) {
            return
        }
        logsNamespace = pod.namespace
        logsPod = pod.name
        logsContainer.model = pod.containers || []
        logsContainer.currentIndex = logsContainer.count > 0 ? 0 : -1
        logsDialog.open()
        backend.startK8sLogs(logsNamespace, logsPod, logsContainer.currentText)
    }

    spacing: 0

    SelectionState { id: podSelection }
    SelectionState { id: serviceSelection }

    onVisibleChanged: {
        if (visible) {
            backend.startK8sWatch()
        } else {
            backend.stopK8sWatch()
        }
    }

    ViewHeader {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.topMargin: 20
        Layout.bottomMargin: 12
        title: qsTr("Kubernetes")
        subtitle: backend.k8sEnabled
                  ? backend.k8sPods.length + qsTr(" pods · ")
                    + backend.k8sServices.length + qsTr(" services")
                  : qsTr("cluster desabilitado")

        Text {
            visible: backend.k8sLoading
            text: qsTr("atualizando…")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
        }

        ActionButton {
            text: qsTr("Habilitar")
            variant: "primary"
            iconName: "play"
            visible: !backend.k8sEnabled
            enabled: !backend.k8sJobActive
            onClicked: backend.enableK8s()
            Accessible.name: qsTr("Habilitar Kubernetes")
        }

        ActionButton {
            text: qsTr("Reset")
            variant: "danger"
            visible: backend.k8sEnabled
            enabled: !backend.k8sJobActive
            onClicked: page.openClusterAction("reset")
            Accessible.name: qsTr("Resetar cluster Kubernetes")
        }

        ActionButton {
            text: qsTr("Desabilitar")
            variant: "ghost"
            visible: backend.k8sEnabled
            enabled: !backend.k8sJobActive
            onClicked: page.openClusterAction("disable")
            Accessible.name: qsTr("Desabilitar Kubernetes")
        }

        ActionButton {
            text: qsTr("Port-forward")
            iconName: "terminal"
            visible: page.tab === 0
            enabled: podsList.currentRow >= 0
            onClicked: page.openPortForward()
            Accessible.name: qsTr("Abrir port-forward do pod")
        }

        ActionButton {
            text: qsTr("Excluir")
            variant: "danger"
            iconName: "trash"
            enabled: page.selectedItem() !== null
            onClicked: page.openDelete()
            Accessible.name: qsTr("Remover recurso selecionado")
        }

        ActionButton {
            text: qsTr("Logs")
            iconName: "terminal"
            enabled: page.tab === 0 && podsList.currentRow >= 0
            onClicked: page.openLogs()
            Accessible.name: qsTr("Ver logs do pod")
        }

        ActionButton {
            variant: "ghost"
            iconName: "refresh"
            onClicked: backend.refreshK8s()
            Accessible.name: qsTr("Atualizar Kubernetes")
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 12
        visible: !backend.k8sEnabled
        implicitHeight: disabledColumn.implicitHeight + 32
        radius: Theme.radiusLg
        color: Theme.bgSurface
        border.width: 1
        border.color: Theme.border

        ColumnLayout {
            id: disabledColumn
            anchors.fill: parent
            anchors.margins: 16
            spacing: 8

            Label {
                text: qsTr("Kubernetes não está habilitado")
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontH2
                font.weight: Font.DemiBold
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("O PodStack cria uma VM dedicada, instala o k3s e gera um kubeconfig isolado. "
                           + "Requer KVM e internet na VM; leva cerca de 1–2 minutos.")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                wrapMode: Text.WordWrap
            }

            ActionButton {
                text: qsTr("Habilitar Kubernetes")
                variant: "primary"
                iconName: "play"
                enabled: !backend.k8sJobActive
                onClicked: backend.enableK8s()
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 8
        visible: backend.k8sJobActive || backend.k8sJobError.length > 0
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: backend.k8sJobStage
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
            }
            Item { Layout.fillWidth: true }
            Label {
                text: backend.k8sJobProgress + "%"
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
            }
        }

        ProgressBar {
            Layout.fillWidth: true
            from: 0
            to: 100
            value: backend.k8sJobProgress
            visible: backend.k8sJobActive
        }

        Label {
            Layout.fillWidth: true
            visible: backend.k8sJobError.length > 0
            text: backend.k8sJobError
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 12
        visible: backend.k8sEnabled
        spacing: 8

        ActionButton {
            text: qsTr("Pods")
            variant: page.tab === 0 ? "primary" : "ghost"
            onClicked: page.tab = 0
        }

        ActionButton {
            text: qsTr("Services")
            variant: page.tab === 1 ? "primary" : "ghost"
            onClicked: page.tab = 1
        }

        Item { Layout.fillWidth: true }
    }

    Label {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 8
        visible: backend.k8sEnabled && backend.k8sError.length > 0
        text: backend.k8sError
        color: Theme.danger
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontMicro
        wrapMode: Text.WordWrap
    }

    StackLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: backend.k8sEnabled
        currentIndex: page.tab

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            CatalogList {
                id: podsList
                anchors.fill: parent
                anchors.leftMargin: 22
                anchors.rightMargin: 22
                anchors.bottomMargin: 20
                emptyIcon: "pods"
                emptyText: qsTr("Nenhum pod.\nHabilite o cluster ou aguarde o primeiro deploy.")
                items: backend.k8sPods
                selection: podSelection
                idKey: "id"
                columns: [
                    { key: "name", label: "POD", width: 0 },
                    { key: "namespace", label: "NAMESPACE", width: 140 },
                    { key: "status", label: "STATUS", width: 110 },
                    { key: "ready", label: "READY", width: 70, mono: true },
                    { key: "restarts", label: "RESTARTS", width: 80, mono: true },
                    { key: "node", label: "NODE", width: 140 },
                    { key: "age", label: "AGE", width: 70, mono: true }
                ]
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            CatalogList {
                id: servicesList
                anchors.fill: parent
                anchors.leftMargin: 22
                anchors.rightMargin: 22
                anchors.bottomMargin: 20
                emptyIcon: "networks"
                emptyText: qsTr("Nenhum service.\nCrie um Service para expor um workload.")
                items: backend.k8sServices
                selection: serviceSelection
                idKey: "id"
                columns: [
                    { key: "name", label: "SERVICE", width: 0 },
                    { key: "namespace", label: "NAMESPACE", width: 140 },
                    { key: "type", label: "TYPE", width: 110 },
                    { key: "clusterIP", label: "CLUSTER-IP", width: 140, mono: true },
                    { key: "ports", label: "PORTS", width: 150, mono: true },
                    { key: "externalIP", label: "EXTERNAL-IP", width: 130, mono: true },
                    { key: "age", label: "AGE", width: 70, mono: true }
                ]
            }
        }
    }

    AppDialog {
        id: logsDialog
        objectName: "k8sLogsDialog"
        title: qsTr("Logs · %1/%2").arg(page.logsNamespace).arg(page.logsPod)
        modal: true
        width: 760
        height: 560

        contentItem: ColumnLayout {
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    text: qsTr("Container")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMeta
                }

                AppSelect {
                    id: logsContainer
                    Layout.preferredWidth: 220
                    onCurrentIndexChanged: {
                        if (logsDialog.opened && currentIndex >= 0) {
                            backend.startK8sLogs(page.logsNamespace, page.logsPod, currentText)
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Label {
                    visible: backend.k8sLogActive
                    text: qsTr("streaming…")
                    color: Theme.cyan
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                }

                ActionButton {
                    text: qsTr("Parar")
                    variant: "secondary"
                    size: "sm"
                    enabled: backend.k8sLogActive
                    onClicked: backend.stopK8sLogs()
                }
            }

            Label {
                Layout.fillWidth: true
                visible: backend.k8sLogError.length > 0
                text: backend.k8sLogError
                color: Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            LogsView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                lines: backend.k8sLogLines
                emptyText: qsTr("Sem logs. O container pode ainda não ter produzido saída.")
                onClearRequested: backend.clearK8sLogs()
            }
        }

        onClosed: backend.stopK8sLogs()
    }

    AppDialog {
        id: clusterActionDialog
        objectName: "k8sClusterActionDialog"
        title: page.clusterActionKind === "reset"
               ? qsTr("Resetar cluster Kubernetes")
               : qsTr("Desabilitar Kubernetes")
        modal: true
        width: 460

        contentItem: ColumnLayout {
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: page.clusterActionKind === "reset"
                      ? qsTr("O cluster, a VM, o disco e o kubeconfig serão removidos. "
                             + "Os dados dos workloads serão perdidos.")
                      : qsTr("A VM do cluster será parada e removida, junto do kubeconfig. "
                             + "Os dados dos workloads serão perdidos.")
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }

                ActionButton {
                    text: qsTr("Cancelar")
                    variant: "ghost"
                    onClicked: clusterActionDialog.close()
                }

                ActionButton {
                    text: page.clusterActionKind === "reset" ? qsTr("Resetar") : qsTr("Desabilitar")
                    variant: "danger"
                    onClicked: {
                        if (page.clusterActionKind === "reset") {
                            backend.resetK8s()
                        } else {
                            backend.disableK8s(true)
                        }
                        clusterActionDialog.close()
                    }
                }
            }
        }
    }

    AppDialog {
        id: deleteDialog
        objectName: "k8sDeleteDialog"
        title: qsTr("Remover %1").arg(page.actionKind)
        modal: true
        width: 420

        contentItem: ColumnLayout {
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: qsTr("Remover %1/%2? A operação não pode ser desfeita.")
                          .arg(page.actionNamespace).arg(page.actionName)
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }

                ActionButton {
                    text: qsTr("Cancelar")
                    variant: "ghost"
                    onClicked: deleteDialog.close()
                }

                ActionButton {
                    text: qsTr("Remover")
                    variant: "danger"
                    onClicked: {
                        if (page.actionKind === "pod") {
                            backend.deleteK8sPod(page.actionNamespace, page.actionName)
                        } else {
                            backend.deleteK8sService(page.actionNamespace, page.actionName)
                        }
                        deleteDialog.close()
                    }
                }
            }
        }
    }

    AppDialog {
        id: forwardDialog
        objectName: "k8sForwardDialog"
        title: qsTr("Port-forward · %1/%2").arg(page.actionNamespace).arg(page.actionName)
        modal: true
        width: 420

        contentItem: ColumnLayout {
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: qsTr("Encaminha uma porta local para o pod via API do cluster.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Label {
                        text: qsTr("Porta local")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }
                    FormField {
                        id: forwardLocal
                        Layout.fillWidth: true
                        inputMethodHints: Qt.ImhDigitsOnly
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Label {
                        text: qsTr("Porta no pod")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }
                    FormField {
                        id: forwardRemote
                        Layout.fillWidth: true
                        inputMethodHints: Qt.ImhDigitsOnly
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: backend.k8sPortForwardActive
                text: qsTr("ativo: 127.0.0.1:%1 → %2").arg(forwardLocal.text)
                          .arg(forwardRemote.text)
                color: Theme.cyan
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
            }

            Label {
                Layout.fillWidth: true
                visible: backend.k8sPortForwardError.length > 0
                text: backend.k8sPortForwardError
                color: Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }

                ActionButton {
                    text: qsTr("Parar")
                    variant: "secondary"
                    enabled: backend.k8sPortForwardActive
                    onClicked: backend.stopK8sPortForward()
                }

                ActionButton {
                    text: qsTr("Iniciar")
                    variant: "primary"
                    enabled: !backend.k8sPortForwardActive
                    onClicked: backend.startK8sPortForward(page.actionNamespace,
                                                           page.actionName,
                                                           parseInt(forwardLocal.text),
                                                           parseInt(forwardRemote.text))
                }

                ActionButton {
                    text: qsTr("Fechar")
                    variant: "ghost"
                    onClicked: forwardDialog.close()
                }
            }
        }

        onClosed: backend.stopK8sPortForward()
    }
}
