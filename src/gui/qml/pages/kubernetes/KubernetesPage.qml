import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

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
            return podsList.currentRow >= 0 ? backend.k8s.pods[podsList.currentRow] : null
        }
        return servicesList.currentRow >= 0 ? backend.k8s.services[servicesList.currentRow] : null
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
        const item = page.selectedItem()
        if (!item) {
            return
        }
        actionNamespace = item.namespace
        actionName = item.name
        actionKind = page.tab === 0 ? "pod" : "service"
        let remote = 80
        if (item.ports) {
            const match = String(item.ports).match(/\d+/)
            if (match) {
                remote = parseInt(match[0])
            }
        }
        forwardLocal.text = "8080"
        forwardRemote.text = String(remote)
        backend.k8s.stopPortForward()
        forwardDialog.open()
    }

    function openClusterAction(kind) {
        clusterActionKind = kind
        clusterActionDialog.open()
    }

    function openLogs() {
        const pod = backend.k8s.pods[podsList.currentRow]
        if (!pod) {
            return
        }
        logsNamespace = pod.namespace
        logsPod = pod.name
        logsContainer.model = pod.containers || []
        logsContainer.currentIndex = logsContainer.count > 0 ? 0 : -1
        logsDialog.open()
        backend.k8s.startLogs(logsNamespace, logsPod, logsContainer.currentText)
    }

    spacing: 0

    SelectionState { id: podSelection }
    SelectionState { id: serviceSelection }

    onVisibleChanged: {
        if (visible) {
            backend.k8s.startWatch()
        } else {
            backend.k8s.stopWatch()
        }
    }

    AppViewHeader {
        objectName: "k8sHeader"
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.topMargin: 20
        Layout.bottomMargin: 12
        Layout.alignment: Qt.AlignTop
        title: qsTr("Kubernetes")
        subtitle: backend.k8s.enabled
                  ? backend.k8s.pods.length + qsTr(" pods · ")
                    + backend.k8s.services.length + qsTr(" services")
                  : qsTr("cluster desabilitado")

        Text {
            visible: backend.k8s.loading
            text: qsTr("atualizando…")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
        }

        AppButton {
            text: qsTr("Habilitar")
            variant: "primary"
            iconName: "play"
            visible: !backend.k8s.enabled
            enabled: !backend.k8s.jobActive
            onClicked: backend.k8s.enable()
            Accessible.name: qsTr("Habilitar Kubernetes")
        }

        AppButton {
            text: qsTr("Reset")
            variant: "danger"
            visible: backend.k8s.enabled
            enabled: !backend.k8s.jobActive
            onClicked: page.openClusterAction("reset")
            Accessible.name: qsTr("Resetar cluster Kubernetes")
        }

        AppButton {
            text: qsTr("Desabilitar")
            variant: "ghost"
            visible: backend.k8s.enabled
            enabled: !backend.k8s.jobActive
            onClicked: page.openClusterAction("disable")
            Accessible.name: qsTr("Desabilitar Kubernetes")
        }

        AppButton {
            text: qsTr("Port-forward")
            iconName: "terminal"
            enabled: page.selectedItem() !== null
            onClicked: page.openPortForward()
            Accessible.name: qsTr("Abrir port-forward do pod")
        }

        AppButton {
            text: qsTr("Excluir")
            variant: "danger"
            iconName: "trash"
            enabled: page.selectedItem() !== null
            onClicked: page.openDelete()
            Accessible.name: qsTr("Remover recurso selecionado")
        }

        AppButton {
            text: qsTr("Logs")
            iconName: "terminal"
            enabled: page.tab === 0 && podsList.currentRow >= 0
            onClicked: page.openLogs()
            Accessible.name: qsTr("Ver logs do pod")
        }

        AppButton {
            variant: "ghost"
            iconName: "refresh"
            onClicked: backend.k8s.refresh()
            Accessible.name: qsTr("Atualizar Kubernetes")
        }
    }

    Rectangle {
        objectName: "k8sDisabledCard"
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 12
        Layout.alignment: Qt.AlignTop
        visible: !backend.k8s.enabled
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
                font.pixelSize: Theme.fontHeading
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

            AppButton {
                text: qsTr("Habilitar Kubernetes")
                variant: "primary"
                iconName: "play"
                enabled: !backend.k8s.jobActive
                onClicked: backend.k8s.enable()
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 8
        visible: backend.k8s.jobActive || backend.k8s.jobError.length > 0
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: backend.k8s.jobStage
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
            }
            Item { Layout.fillWidth: true }
            Label {
                text: backend.k8s.jobProgress + "%"
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
            }
        }

        ProgressBar {
            Layout.fillWidth: true
            from: 0
            to: 100
            value: backend.k8s.jobProgress
            visible: backend.k8s.jobActive
        }

        Label {
            Layout.fillWidth: true
            visible: backend.k8s.jobError.length > 0
            text: backend.k8s.jobError
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }
    }

    AppTabs {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 12
        visible: backend.k8s.enabled
        model: [qsTr("Pods"), qsTr("Services")]
        currentIndex: page.tab
        onActivated: function(index) { page.tab = index }
    }

    Label {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 8
        visible: backend.k8s.enabled && backend.k8s.error.length > 0
        text: backend.k8s.error
        color: Theme.danger
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontMicro
        wrapMode: Text.WordWrap
    }

    StackLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: backend.k8s.enabled
        currentIndex: page.tab

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            AppCatalogList {
                id: podsList
                anchors.fill: parent
                anchors.leftMargin: 22
                anchors.rightMargin: 22
                anchors.bottomMargin: 20
                emptyIcon: "pods"
                emptyText: qsTr("Nenhum pod.\nHabilite o cluster ou aguarde o primeiro deploy.")
                items: backend.k8s.pods
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

            AppCatalogList {
                id: servicesList
                anchors.fill: parent
                anchors.leftMargin: 22
                anchors.rightMargin: 22
                anchors.bottomMargin: 20
                emptyIcon: "networks"
                emptyText: qsTr("Nenhum service.\nCrie um Service para expor um workload.")
                items: backend.k8s.services
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

    AppModal {
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
                            backend.k8s.startLogs(page.logsNamespace, page.logsPod, currentText)
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Label {
                    visible: backend.k8s.logActive
                    text: qsTr("streaming…")
                    color: Theme.cyan
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                }

                AppButton {
                    text: qsTr("Parar")
                    variant: "secondary"
                    size: "sm"
                    enabled: backend.k8s.logActive
                    onClicked: backend.k8s.stopLogs()
                }
            }

            Label {
                Layout.fillWidth: true
                visible: backend.k8s.logError.length > 0
                text: backend.k8s.logError
                color: Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            AppLogView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                lines: backend.k8s.logLines
                emptyText: qsTr("Sem logs. O container pode ainda não ter produzido saída.")
                onClearRequested: backend.k8s.clearLogs()
            }
        }

        onClosed: backend.k8s.stopLogs()
    }

    AppModal {
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

                AppButton {
                    text: qsTr("Cancelar")
                    variant: "ghost"
                    onClicked: clusterActionDialog.close()
                }

                AppButton {
                    text: page.clusterActionKind === "reset" ? qsTr("Resetar") : qsTr("Desabilitar")
                    variant: "danger"
                    onClicked: {
                        if (page.clusterActionKind === "reset") {
                            backend.k8s.reset()
                        } else {
                            backend.k8s.disable(true)
                        }
                        clusterActionDialog.close()
                    }
                }
            }
        }
    }

    AppModal {
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

                AppButton {
                    text: qsTr("Cancelar")
                    variant: "ghost"
                    onClicked: deleteDialog.close()
                }

                AppButton {
                    text: qsTr("Remover")
                    variant: "danger"
                    onClicked: {
                        if (page.actionKind === "pod") {
                            backend.k8s.deletePod(page.actionNamespace, page.actionName)
                        } else {
                            backend.k8s.deleteService(page.actionNamespace, page.actionName)
                        }
                        deleteDialog.close()
                    }
                }
            }
        }
    }

    AppModal {
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
                    AppInput {
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
                    AppInput {
                        id: forwardRemote
                        Layout.fillWidth: true
                        inputMethodHints: Qt.ImhDigitsOnly
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: backend.k8s.portForwardActive
                text: qsTr("ativo: 127.0.0.1:%1 → %2").arg(forwardLocal.text)
                          .arg(forwardRemote.text)
                color: Theme.cyan
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
            }

            Label {
                Layout.fillWidth: true
                visible: backend.k8s.portForwardError.length > 0
                text: backend.k8s.portForwardError
                color: Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }

                AppButton {
                    text: qsTr("Parar")
                    variant: "secondary"
                    enabled: backend.k8s.portForwardActive
                    onClicked: backend.k8s.stopPortForward()
                }

                AppButton {
                    text: qsTr("Iniciar")
                    variant: "primary"
                    enabled: !backend.k8s.portForwardActive
                    onClicked: backend.k8s.startPortForward(page.actionNamespace,
                                                           page.actionName,
                                                           parseInt(forwardLocal.text),
                                                           parseInt(forwardRemote.text),
                                                           page.actionKind)
                }

                AppButton {
                    text: qsTr("Fechar")
                    variant: "ghost"
                    onClicked: forwardDialog.close()
                }
            }
        }

        onClosed: backend.k8s.stopPortForward()
    }

    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: !backend.k8s.enabled
    }
}
