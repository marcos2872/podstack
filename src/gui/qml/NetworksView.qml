import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: networksView

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
                text: "Networks"
                color: theme.textPrimary
                font.pixelSize: 15
                font.bold: true
            }

            Label {
                text: backend.networks.length + " itens"
                color: theme.textSecondary
                font.pixelSize: 11
            }

            Item { Layout.fillWidth: true }

            ActionButton {
                text: "Criar"
                onClicked: {
                    networkName.text = ""
                    networkSubnet.text = ""
                    networkGateway.text = ""
                    createDialog.open()
                }
            }

            ActionButton {
                text: "Conectar"
                accentColor: theme.indigo500
                enabled: networksList.currentRow >= 0
                onClicked: {
                    connectNetworkName.text = backend.networks[networksList.currentRow].name
                    connectContainer.text = ""
                    connectDialog.open()
                }
            }

            ActionButton {
                text: "Desconectar"
                accentColor: theme.bgElevated
                enabled: networksList.currentRow >= 0
                onClicked: {
                    disconnectNetworkName.text = backend.networks[networksList.currentRow].name
                    disconnectContainer.text = ""
                    disconnectDialog.open()
                }
            }

            ActionButton {
                text: "Remover"
                accentColor: theme.danger
                enabled: networksList.currentRow >= 0
                onClicked: removeDialog.open()
            }

            ActionButton {
                text: "Atualizar"
                accentColor: theme.bgElevated
                onClicked: backend.refreshNetworks()
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: theme.border
    }

    CatalogList {
        id: networksList
        Layout.fillWidth: true
        Layout.fillHeight: true
        items: backend.networks
        columns: [
            { key: "name", label: "NAME", width: 0 },
            { key: "driver", label: "DRIVER", width: 80 },
            { key: "subnet", label: "SUBNET", width: 160, mono: true },
            { key: "containers", label: "CONTAINERS", width: 90 }
        ]
    }

    Label {
        Layout.fillWidth: true
        Layout.margins: 24
        visible: backend.networks.length === 0
        horizontalAlignment: Text.AlignHCenter
        text: "Nenhuma rede.\nCrie uma rede bridge para isolar containers."
        color: theme.textSecondary
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }

    Dialog {
        id: createDialog
        title: "Nova rede"
        modal: true
        width: 460
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
                id: networkName
                Layout.fillWidth: true
                placeholderText: "backend"
            }
            Label {
                text: "Subnet (CIDR, opcional)"
                color: theme.textSecondary
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
                color: theme.textSecondary
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
                accentColor: theme.bgElevated
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
            color: theme.bgSurface
            radius: 10
            border.color: theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: "Rede"
                color: theme.textSecondary
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
                color: theme.textSecondary
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
                accentColor: theme.bgElevated
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
            color: theme.bgSurface
            radius: 10
            border.color: theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: "Rede"
                color: theme.textSecondary
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
                color: theme.textSecondary
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
                accentColor: theme.bgElevated
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
            color: theme.bgSurface
            radius: 10
            border.color: theme.border
        }
        contentItem: Label {
            text: "Containers conectados bloqueiam a remoção."
            color: theme.textPrimary
            wrapMode: Text.WordWrap
        }
        onAccepted: {
            if (networksList.currentRow >= 0) {
                backend.removeNetwork(backend.networks[networksList.currentRow].name, false)
            }
        }
    }
}
