import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Diálogo de dispositivos USB da machine (anexar/desanexar via backend).
AppModal {
    id: root

    property string pendingId: ""
    property bool actionPending: false

    function openFor(id) {
        root.pendingId = id
        root.actionPending = false
        usbError.text = ""
        backend.machines.refreshUsbDevices(id)
        backend.machines.loadInfo(id)
        root.open()
    }

    function attachUsbDevice(bus, address) {
        if (root.pendingId.length === 0 || root.actionPending) {
            return
        }
        root.actionPending = true
        usbError.text = ""
        backend.machines.attachUsbDevice(root.pendingId, bus, address)
    }

    function detachUsbDevice(bus, address) {
        if (root.pendingId.length === 0 || root.actionPending) {
            return
        }
        root.actionPending = true
        usbError.text = ""
        backend.machines.detachUsbDevice(root.pendingId, bus, address)
    }

    title: qsTr("Dispositivos USB")
    modal: true
    width: 560
    anchors.centerIn: parent
    closePolicy: root.actionPending ? Popup.NoAutoClose
                                    : Popup.CloseOnEscape
                                      | Popup.CloseOnPressOutside
    onClosed: {
        root.pendingId = ""
        root.actionPending = false
        usbError.text = ""
    }

    Connections {
        target: backend.machines

        function onActionFinished(ok, message) {
            if (!root.actionPending) {
                return
            }
            root.actionPending = false
            if (ok) {
                usbError.text = ""
                backend.machines.refreshUsbDevices(root.pendingId)
                backend.machines.loadInfo(root.pendingId)
            } else {
                usbError.text = message.length > 0 ? message : backend.machines.lastError
            }
            if (!root.opened) {
                root.pendingId = ""
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 9

        Text {
            Layout.fillWidth: true
            text: qsTr("Anexe dispositivos USB do host (ex.: pendrive) à máquina. "
                       + "A máquina reinicia ao anexar ou desanexar se estiver rodando.")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        Text {
            text: qsTr("Anexados")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            font.weight: Font.DemiBold
        }

        Text {
            Layout.fillWidth: true
            visible: (backend.machines.info.usbDevices || []).length === 0
            text: qsTr("Nenhum dispositivo USB anexado.")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
        }

        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(160, Math.max(1,
                (backend.machines.info.usbDevices || []).length) * 36)
            clip: true
            model: backend.machines.info.usbDevices || []
            delegate: RowLayout {
                required property var modelData
                width: ListView.view.width
                height: 36
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: String(modelData.bus) + ":" + String(modelData.address)
                          + "  " + String(modelData.description || modelData.vendorId)
                    color: Theme.textPrimary
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontMeta
                    elide: Text.ElideMiddle
                }
                AppButton {
                    text: qsTr("Desanexar")
                    size: "sm"
                    variant: "danger"
                    enabled: !root.actionPending
                    onClicked: root.detachUsbDevice(Number(modelData.bus),
                                                    Number(modelData.address))
                }
            }
        }

        Text {
            text: qsTr("Disponíveis no host")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            font.weight: Font.DemiBold
        }

        Text {
            Layout.fillWidth: true
            visible: backend.machines.usbDevices.length === 0
            text: qsTr("Nenhum dispositivo USB disponível no host.")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
        }

        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(220, Math.max(1,
                backend.machines.usbDevices.length) * 36)
            clip: true
            model: backend.machines.usbDevices
            delegate: RowLayout {
                required property var modelData
                width: ListView.view.width
                height: 36
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: String(modelData.bus) + ":" + String(modelData.address)
                          + "  " + String(modelData.description || modelData.vendorId)
                    color: Theme.textPrimary
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontMeta
                    elide: Text.ElideMiddle
                }
                AppButton {
                    text: qsTr("Anexar")
                    size: "sm"
                    enabled: !root.actionPending
                    onClicked: root.attachUsbDevice(Number(modelData.bus),
                                                    Number(modelData.address))
                }
            }
        }

        Text {
            id: usbError
            Layout.fillWidth: true
            visible: text.length > 0
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Fechar")
                onClicked: root.close()
            }
        }
    }
}
