import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: page

    property int tab: 0

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
        subtitle: backend.k8sPods.length + qsTr(" pods · ")
                  + backend.k8sServices.length + qsTr(" services")

        Text {
            visible: backend.k8sLoading
            text: qsTr("atualizando…")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
        }

        ActionButton {
            variant: "ghost"
            iconName: "refresh"
            onClicked: backend.refreshK8s()
            Accessible.name: qsTr("Atualizar Kubernetes")
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 12
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
}
