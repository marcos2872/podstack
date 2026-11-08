import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: networksView


    spacing: 0

    ViewHeader {
        Layout.fillWidth: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.topMargin: 20
        Layout.bottomMargin: 16
        title: qsTr("Networks")
        subtitle: backend.networks.length + qsTr(" redes")

        ActionButton {
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

        ActionButton {
            text: qsTr("Conectar")
            enabled: networksList.currentRow >= 0
            onClicked: {
                connectNetworkName.text = backend.networks[networksList.currentRow].name
                connectContainer.text = ""
                connectDialog.open()
            }
        }

        ActionButton {
            text: qsTr("Desconectar")
            enabled: networksList.currentRow >= 0
            onClicked: {
                disconnectNetworkName.text = backend.networks[networksList.currentRow].name
                disconnectContainer.text = ""
                disconnectDialog.open()
            }
        }

        ActionButton {
            text: qsTr("Remover")
            variant: "danger"
            enabled: networksList.currentRow >= 0
            onClicked: removeDialog.open()
        }

        ActionButton {
            variant: "ghost"
            iconName: "refresh"
            onClicked: backend.refreshNetworks()
            Accessible.name: qsTr("Atualizar")
        }
    }

    CatalogList {
        id: networksList
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.leftMargin: 22
        Layout.rightMargin: 22
        Layout.bottomMargin: 20
        emptyIcon: "networks"
            emptyText: qsTr("Nenhuma rede.\nCrie uma rede bridge para isolar containers.")
        items: backend.networks
        columns: [
            { key: "name", label: "NAME", width: 0 },
            { key: "driver", label: "DRIVER", width: 80 },
            { key: "subnet", label: "SUBNET", width: 160, mono: true },
            { key: "containers", label: "CONTAINERS", width: 90 }
        ]
    }

    Dialog {
        id: createDialog
        title: "Nova rede"
        modal: true
        width: 460
        background: Rectangle {
            color: Theme.bgSurface
            radius: 10
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
                id: networkName
                Layout.fillWidth: true
                placeholderText: "backend"
            }
            Label {
                text: "Subnet (CIDR, opcional)"
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            FormField {
                id: networkSubnet
                Layout.fillWidth: true
                placeholderText: "10.89.0.0/24"
            }
            Label {
                text: "Gateway (opcional)"
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            FormField {
                id: networkGateway
                Layout.fillWidth: true
                placeholderText: "10.89.0.1"
            }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: "Cancelar"
                variant: "secondary"
                onClicked: createDialog.close()
            }
            ActionButton {
                text: "Criar"
                enabled: networkName.text.length > 0
                onClicked: {
                    backend.createNetwork(networkName.text, networkSubnet.text,
                                          networkGateway.text)
                    createDialog.close()
                }
            }
        }
    }

    Dialog {
        id: connectDialog
        title: "Conectar container"
        modal: true
        width: 440
        background: Rectangle {
            color: Theme.bgSurface
            radius: 10
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: "Rede"
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            FormField {
                id: connectNetworkName
                Layout.fillWidth: true
                readOnly: true
            }
            Label {
                text: "Container (ID ou nome)"
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            FormField {
                id: connectContainer
                Layout.fillWidth: true
            }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: "Cancelar"
                variant: "secondary"
                onClicked: connectDialog.close()
            }
            ActionButton {
                text: "Conectar"
                enabled: connectContainer.text.length > 0
                onClicked: {
                    backend.connectNetwork(connectNetworkName.text, connectContainer.text)
                    connectDialog.close()
                }
            }
        }
    }

    Dialog {
        id: disconnectDialog
        title: "Desconectar container"
        modal: true
        width: 440
        background: Rectangle {
            color: Theme.bgSurface
            radius: 10
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: "Rede"
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            FormField {
                id: disconnectNetworkName
                Layout.fillWidth: true
                readOnly: true
            }
            Label {
                text: "Container (ID ou nome)"
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            FormField {
                id: disconnectContainer
                Layout.fillWidth: true
            }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: "Cancelar"
                variant: "secondary"
                onClicked: disconnectDialog.close()
            }
            ActionButton {
                text: "Desconectar"
                enabled: disconnectContainer.text.length > 0
                onClicked: {
                    backend.disconnectNetwork(disconnectNetworkName.text,
                                               disconnectContainer.text)
                    disconnectDialog.close()
                }
            }
        }
    }

    Dialog {
        id: removeDialog
        title: "Remover rede?"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle {
            color: Theme.bgSurface
            radius: 10
            border.color: Theme.border
        }
        contentItem: Label {
            text: "Containers conectados bloqueiam a remoção."
            color: Theme.textPrimary
            wrapMode: Text.WordWrap
        }
        onAccepted: {
            if (networksList.currentRow >= 0) {
                backend.removeNetwork(backend.networks[networksList.currentRow].name, false)
            }
        }
    }
}
