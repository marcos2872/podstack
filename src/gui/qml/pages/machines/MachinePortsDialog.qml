import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Diálogo de portas encaminhadas da machine. Mantém o rascunho local, envia
// via `machines.setPorts` e reage ao resultado pela ação do backend.
AppModal {
    id: root

    property var selectedMachine: ({})

    property string pendingId: ""
    property bool actionPending: false
    property var draft: []

    function openFor(id, machine) {
        root.pendingId = id
        root.actionPending = false
        root.draft = []
        let current = machine || root.selectedMachine
        for (let i = 0; i < backend.machines.items.length; ++i) {
            if (String(backend.machines.items[i].id) === id) {
                current = backend.machines.items[i]
                break
            }
        }
        const ports = current && current.ports ? current.ports : []
        const rows = []
        for (let i = 0; i < ports.length; ++i) {
            const port = ports[i]
            const protocol = port.protocol ? String(port.protocol) : "tcp"
            if (protocol === "tcp" && Number(port.guestPort) === 22) {
                continue
            }
            rows.push({
                "id": port.id ? String(port.id) : "",
                "hostPort": Number(port.hostPort || 0),
                "guestPort": Number(port.guestPort || 0),
                "protocol": protocol,
                "exposeLan": Boolean(port.exposeLan),
                "label": port.label ? String(port.label) : ""
            })
        }
        root.draft = rows
        portsHost.text = ""
        portsGuest.text = ""
        portsProtocol.currentIndex = 0
        portsLabel.text = ""
        portsLan.checked = false
        portsError.text = ""
        root.open()
    }

    function addPortDraft() {
        const hostPort = parseInt(portsHost.text)
        const guestPort = parseInt(portsGuest.text)
        if (isNaN(hostPort) || hostPort < 1 || hostPort > 65535) {
            portsError.text = qsTr("Informe a porta do host (1..65535).")
            return
        }
        if (isNaN(guestPort) || guestPort < 1 || guestPort > 65535) {
            portsError.text = qsTr("Informe a porta do guest (1..65535).")
            return
        }
        const protocol = portsProtocol.currentText
        for (let i = 0; i < root.draft.length; ++i) {
            if (Number(root.draft[i].hostPort) === hostPort
                    && String(root.draft[i].protocol) === protocol) {
                portsError.text = qsTr("Essa porta do host já está na lista para o protocolo.")
                return
            }
        }
        root.draft = root.draft.concat([{
            "id": "",
            "hostPort": hostPort,
            "guestPort": guestPort,
            "protocol": protocol,
            "exposeLan": portsLan.checked,
            "label": portsLabel.text.trim()
        }])
        portsHost.text = ""
        portsGuest.text = ""
        portsLabel.text = ""
        portsLan.checked = false
        portsError.text = ""
    }

    function removePortDraft(index) {
        const next = []
        for (let i = 0; i < root.draft.length; ++i) {
            if (i !== index) {
                next.push(root.draft[i])
            }
        }
        root.draft = next
    }

    title: qsTr("Portas da machine")
    modal: true
    width: 560
    anchors.centerIn: parent
    closePolicy: root.actionPending ? Popup.NoAutoClose
                                    : Popup.CloseOnEscape
                                      | Popup.CloseOnPressOutside
    onClosed: {
        root.pendingId = ""
        root.actionPending = false
        root.draft = []
        portsError.text = ""
    }
    background: Rectangle {
        color: Theme.bgSurface
        radius: Theme.radiusLg
        border.width: 1
        border.color: Theme.border
    }

    Connections {
        target: backend.machines

        function onActionFinished(ok, message) {
            if (!root.actionPending || !root.opened) {
                return
            }
            root.actionPending = false
            if (ok) {
                root.close()
            } else {
                portsError.text = message.length > 0 ? message : backend.machines.lastError
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 9

        Text {
            Layout.fillWidth: true
            text: qsTr("A porta SSH (22/tcp) é gerenciada automaticamente e não aparece na lista.")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(170, root.draft.length * 36 + 8)
            visible: root.draft.length > 0
            radius: Theme.radiusSm
            color: Theme.bgSurface2
            border.width: 1
            border.color: Theme.border

            ListView {
                id: portsDraftList
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: root.draft
                spacing: 2

                delegate: RowLayout {
                    required property var modelData
                    required property int index

                    width: portsDraftList.width
                    spacing: 8

                    Text {
                        Layout.preferredWidth: 76
                        text: modelData.hostPort + " → " + modelData.guestPort
                        color: Theme.textSecondary
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontMicro
                    }

                    Text {
                        Layout.preferredWidth: 40
                        text: modelData.protocol
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }

                    Text {
                        Layout.fillWidth: true
                        text: modelData.exposeLan ? qsTr("0.0.0.0 (LAN)")
                                                  : qsTr("127.0.0.1")
                        color: modelData.exposeLan ? Theme.warning : Theme.textTertiary
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontMicro
                    }

                    Text {
                        Layout.preferredWidth: 110
                        text: modelData.label
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        elide: Text.ElideRight
                    }

                    AppButton {
                        text: qsTr("Remover")
                        size: "sm"
                        variant: "ghost"
                        enabled: !root.actionPending
                        onClicked: root.removePortDraft(index)
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.draft.length === 0
            text: qsTr("Nenhuma porta extra configurada.")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 7

            Text { text: qsTr("Porta do host"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
            AppInput {
                id: portsHost
                Layout.fillWidth: true
                enabled: !root.actionPending
                validator: IntValidator { bottom: 1; top: 65535 }
            }

            Text { text: qsTr("Porta do guest"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
            AppInput {
                id: portsGuest
                Layout.fillWidth: true
                enabled: !root.actionPending
                validator: IntValidator { bottom: 1; top: 65535 }
            }

            Text { text: qsTr("Protocolo"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
            AppSelect {
                id: portsProtocol
                Layout.fillWidth: true
                model: ["tcp", "udp"]
                currentIndex: 0
                enabled: !root.actionPending
            }

            Text { text: qsTr("Rótulo"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
            AppInput {
                id: portsLabel
                Layout.fillWidth: true
                placeholderText: qsTr("opcional (até 64 caracteres)")
                enabled: !root.actionPending
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            AppCheck {
                id: portsLan
                text: qsTr("Expor na LAN (0.0.0.0)")
                enabled: !root.actionPending
            }

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("Adicionar")
                size: "sm"
                variant: "secondary"
                enabled: !root.actionPending
                onClicked: root.addPortDraft()
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Aviso: expor na LAN faz o QEMU escutar em 0.0.0.0 e permite que outros dispositivos acessem a porta; o firewall do host também precisa liberá-la.")
            color: Theme.warning
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        Text {
            id: portsError
            Layout.fillWidth: true
            visible: text.length > 0
            text: ""
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }
    }

    footer: AppFooter {
        Item { Layout.fillWidth: true }
        AppButton {
            text: qsTr("Cancelar")
            variant: "secondary"
            enabled: !root.actionPending
            onClicked: root.close()
        }
        AppButton {
            text: qsTr("Aplicar")
            variant: "primary"
            enabled: !root.actionPending && root.pendingId.length > 0
            onClicked: {
                portsError.text = ""
                root.actionPending = true
                backend.machines.setPorts(root.pendingId, root.draft)
            }
        }
    }
}
