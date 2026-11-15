import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Diálogo de portas seriais da machine (anexar/desanexar via backend).
AppModal {
    id: root

    property string pendingId: ""
    property bool actionPending: false

    function openFor(id) {
        root.pendingId = id
        root.actionPending = false
        serialError.text = ""
        backend.machines.refreshSerialPorts(id)
        backend.machines.loadInfo(id)
        root.open()
    }

    function attachSerialPort(path) {
        if (root.pendingId.length === 0 || root.actionPending) {
            return
        }
        root.actionPending = true
        serialError.text = ""
        backend.machines.attachSerialPort(root.pendingId, path)
    }

    function detachSerialPort(path) {
        if (root.pendingId.length === 0 || root.actionPending) {
            return
        }
        root.actionPending = true
        serialError.text = ""
        backend.machines.detachSerialPort(root.pendingId, path)
    }

    title: qsTr("Portas seriais")
    modal: true
    width: 560
    anchors.centerIn: parent
    closePolicy: root.actionPending ? Popup.NoAutoClose
                                    : Popup.CloseOnEscape
                                      | Popup.CloseOnPressOutside
    onClosed: {
        root.pendingId = ""
        root.actionPending = false
        serialError.text = ""
    }

    Connections {
        target: backend

        function onActionFinished(ok, message) {
            if (!root.actionPending) {
                return
            }
            root.actionPending = false
            if (ok) {
                serialError.text = ""
                backend.machines.refreshSerialPorts(root.pendingId)
                backend.machines.loadInfo(root.pendingId)
            } else {
                serialError.text = message.length > 0 ? message : backend.lastError
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
            text: qsTr("Anexe dispositivos seriais do host (ttyUSB/ttyACM/UART). "
                       + "A máquina reinicia ao anexar ou desanexar se estiver rodando.")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        Text {
            text: qsTr("Anexadas")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            font.weight: Font.DemiBold
        }

        Text {
            Layout.fillWidth: true
            visible: (backend.machines.info.serialPorts || []).length === 0
            text: qsTr("Nenhuma porta anexada.")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
        }

        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(160, Math.max(1,
                (backend.machines.info.serialPorts || []).length) * 36)
            clip: true
            model: backend.machines.info.serialPorts || []
            delegate: RowLayout {
                required property var modelData
                width: ListView.view.width
                height: 36
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: String(modelData)
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
                    onClicked: root.detachSerialPort(String(modelData))
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
            visible: backend.machines.serialPorts.length === 0
            text: qsTr("Nenhuma porta serial disponível no host.")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
        }

        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(220, Math.max(1,
                backend.machines.serialPorts.length) * 36)
            clip: true
            model: backend.machines.serialPorts
            delegate: RowLayout {
                required property var modelData
                width: ListView.view.width
                height: 36
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: String(modelData.path) + "  " + String(modelData.description || "")
                    color: Theme.textPrimary
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontMeta
                    elide: Text.ElideMiddle
                }
                AppButton {
                    text: qsTr("Anexar")
                    size: "sm"
                    enabled: !root.actionPending
                             && modelData.readable === true
                             && !(backend.machines.info.serialPorts || []).includes(
                                 String(modelData.path))
                    onClicked: root.attachSerialPort(String(modelData.path))
                }
            }
        }

        Text {
            id: serialError
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
