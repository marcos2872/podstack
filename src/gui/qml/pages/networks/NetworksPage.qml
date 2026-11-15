import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

ColumnLayout {
    id: networksView


    spacing: 0

    SelectionState { id: networkSelection }

    AppViewHeader {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.topMargin: 20
        Layout.bottomMargin: 16
        title: qsTr("Networks")
        subtitle: backend.networks.items.length + qsTr(" redes")

        AppButton {
            text: qsTr("Criar")
            variant: "primary"
            iconName: "plus"
            onClicked: {
                networkName.text = ""
                networkSubnet.text = ""
                networkGateway.text = ""
                createDialog.open()
            }
        }

        AppButton {
            text: qsTr("Conectar")
            enabled: networksList.currentRow >= 0
            onClicked: {
                connectNetworkName.text = backend.networks.items[networksList.currentRow].name
                connectContainer.text = ""
                connectDialog.open()
            }
        }

        AppButton {
            text: qsTr("Desconectar")
            enabled: networksList.currentRow >= 0
            onClicked: {
                disconnectNetworkName.text = backend.networks.items[networksList.currentRow].name
                disconnectContainer.text = ""
                disconnectDialog.open()
            }
        }

        Text {
            visible: networkSelection.count > 0
            text: networkSelection.count + qsTr(" selecionadas")
            color: Theme.indigo400
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMeta
        }

        AppButton {
            visible: networkSelection.count > 0
            text: qsTr("Remover")
            variant: "danger"
            iconName: "trash"
            onClicked: batchRemoveDialog.open()
            Accessible.name: qsTr("Remover redes selecionadas")
        }

        AppButton {
            visible: networkSelection.count === 0
            text: qsTr("Remover")
            variant: "danger"
            enabled: networksList.currentRow >= 0
            onClicked: removeDialog.open()
        }

        AppButton {
            variant: "ghost"
            iconName: "refresh"
            onClicked: backend.networks.refresh()
            Accessible.name: qsTr("Atualizar")
        }
    }

    AppErrorBanner {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 8
        message: backend.networks.lastError
        actionText: qsTr("Dispensar")
        onActionClicked: backend.networks.dismissError()
    }

    AppCatalogList {
        id: networksList
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 20
        emptyIcon: "networks"
            emptyText: qsTr("Nenhuma rede.\nCrie uma rede bridge para isolar containers.")
        items: backend.networks.items
        selection: networkSelection
        idKey: "name"
        columns: [
            { key: "name", label: "NAME", width: 0 },
            { key: "driver", label: "DRIVER", width: 80 },
            { key: "subnet", label: "SUBNET", width: 160, mono: true },
            { key: "containers", label: "CONTAINERS", width: 90 }
        ]
    }

    AppModal {
        id: createDialog
        title: qsTr("Nova rede")
        modal: true
        width: 460
        actionController: backend.networks
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
                id: networkName
                Layout.fillWidth: true
                placeholderText: "backend"
            }
            Label {
                text: qsTr("Subnet (CIDR, opcional)")
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            AppInput {
                id: networkSubnet
                Layout.fillWidth: true
                placeholderText: "10.89.0.0/24"
            }
            Label {
                text: qsTr("Gateway (opcional)")
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            AppInput {
                id: networkGateway
                Layout.fillWidth: true
                placeholderText: "10.89.0.1"
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
                enabled: !createDialog.busy && networkName.text.length > 0
                onClicked: createDialog.runAction(function() {
                    backend.networks.createNetwork(networkName.text, networkSubnet.text,
                                                   networkGateway.text)
                }, "networks.create")
            }
        }
    }

    AppModal {
        id: connectDialog
        title: qsTr("Conectar container")
        modal: true
        width: 440
        actionController: backend.networks
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: qsTr("Rede")
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            AppInput {
                id: connectNetworkName
                Layout.fillWidth: true
                readOnly: true
            }
            Label {
                text: qsTr("Container (ID ou nome)")
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            AppInput {
                id: connectContainer
                Layout.fillWidth: true
            }
        }
        footer: AppFooter {
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                enabled: !connectDialog.busy
                onClicked: connectDialog.close()
            }
            AppButton {
                text: qsTr("Conectar")
                busy: connectDialog.busy
                busyText: qsTr("Conectando…")
                enabled: !connectDialog.busy && connectContainer.text.length > 0
                onClicked: connectDialog.runAction(function() {
                    backend.networks.connectNetwork(connectNetworkName.text, connectContainer.text)
                }, "networks.connect")
            }
        }
    }

    AppModal {
        id: disconnectDialog
        title: qsTr("Desconectar container")
        modal: true
        width: 440
        actionController: backend.networks
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: qsTr("Rede")
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            AppInput {
                id: disconnectNetworkName
                Layout.fillWidth: true
                readOnly: true
            }
            Label {
                text: qsTr("Container (ID ou nome)")
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            AppInput {
                id: disconnectContainer
                Layout.fillWidth: true
            }
        }
        footer: AppFooter {
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                enabled: !disconnectDialog.busy
                onClicked: disconnectDialog.close()
            }
            AppButton {
                text: qsTr("Desconectar")
                busy: disconnectDialog.busy
                busyText: qsTr("Desconectando…")
                enabled: !disconnectDialog.busy && disconnectContainer.text.length > 0
                onClicked: disconnectDialog.runAction(function() {
                    backend.networks.disconnectNetwork(disconnectNetworkName.text,
                                                       disconnectContainer.text)
                }, "networks.disconnect")
            }
        }
    }

    AppConfirmDialog {
        id: removeDialog
        title: qsTr("Remover rede?")
        message: qsTr("Containers conectados bloqueiam a remoção.")
        confirmText: qsTr("Remover")
        destructive: true
        onConfirmed: {
            if (networksList.currentRow >= 0) {
                backend.networks.removeNetwork(backend.networks.items[networksList.currentRow].name, false)
            }
        }
    }

    AppConfirmDialog {
        id: batchRemoveDialog
        title: qsTr("Remover redes selecionadas")
        message: qsTr("%1 rede(s) serão removidas. Containers conectados perdem o vínculo.")
                     .arg(networkSelection.count)
        confirmText: qsTr("Remover")
        destructive: true
        busyText: qsTr("Removendo…")
        actionController: backend.networks
        actionMethod: "networks.batch"
        onConfirmed: backend.networks.batchNetworks(networkSelection.ids(), false)
    }
}
