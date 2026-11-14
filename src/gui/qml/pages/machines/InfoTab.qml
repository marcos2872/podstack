import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Aba "Info" do inspetor de machine: dados da machine, agent SSH, portas,
// serial, USB e mounts. Tudo vem da propriedade `machine`.
Flickable {
    id: root

    property var machine: ({})
    property string machineId: ""
    property string machineState: ""

    readonly property var portsList: machine && machine.ports ? machine.ports : []
    readonly property var mountsList: machine && machine.mounts ? machine.mounts : []

    // O clique no checkbox quebra o binding de `checked`. Reaplica quando a
    // machine muda para o controle voltar a refletir o estado persistido.
    onMachineChanged: {
        if (forwardAgentCheck) {
            forwardAgentCheck.checked = Qt.binding(function() {
                return root.machine.forwardSshAgent === true
            })
        }
    }

    function formatGiB(bytes) {
        const value = Number(bytes || 0) / 1073741824
        return value > 0 ? value.toFixed(1) + " GiB" : "-"
    }

    function portsText() {
        if (portsList.length === 0) {
            return "-"
        }
        const values = []
        for (let i = 0; i < portsList.length; ++i) {
            const port = portsList[i]
            const label = port.label ? String(port.label) + ": " : ""
            values.push(label + "127.0.0.1:" + port.hostPort + " → "
                        + port.guestPort + "/" + port.protocol
                        + (port.exposeLan ? qsTr(" (LAN)") : ""))
        }
        return values.join("\n")
    }

    function mountsText() {
        if (mountsList.length === 0) {
            return "-"
        }
        const values = []
        for (let i = 0; i < mountsList.length; ++i) {
            const mount = mountsList[i]
            values.push(mount.source + " → " + mount.target
                        + (mount.readOnly ? qsTr(" (somente leitura)") : ""))
        }
        return values.join("\n")
    }

    function serialText() {
        const serial = root.machine && root.machine.serialPorts ? root.machine.serialPorts : []
        if (serial.length === 0) {
            return "-"
        }
        return serial.join("\n")
    }

    function usbText() {
        const devices =
            root.machine && root.machine.usbDevices ? root.machine.usbDevices : []
        const values = []
        for (let i = 0; i < devices.length; ++i) {
            const device = devices[i]
            values.push(String(device.bus) + ":" + String(device.address) + " "
                        + String(device.description || device.vendorId))
        }
        return values.length === 0 ? "-" : values.join("\n")
    }

    clip: true
    contentWidth: width
    contentHeight: infoColumn.implicitHeight + 26
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    ColumnLayout {
        id: infoColumn

        x: 14
        y: 12
        width: root.width - 28
        spacing: 12

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 18
            rowSpacing: 7

            Text { text: qsTr("Distro"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
            Text { Layout.fillWidth: true; text: root.machine.distroLabel || "-"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta; elide: Text.ElideRight }

            Text { text: qsTr("Arch"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
            Text { Layout.fillWidth: true; text: root.machine.arch || "-"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }

            Text { text: qsTr("CPUs"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
            Text { Layout.fillWidth: true; text: root.machine.cpus || "-"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }

            Text { text: qsTr("Memória"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
            Text { Layout.fillWidth: true; text: root.formatGiB(root.machine.memoryBytes); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }

            Text { text: qsTr("Disco"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
            Text { Layout.fillWidth: true; text: root.formatGiB(root.machine.diskBytes); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }

            Text { text: qsTr("SSH"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
            Text { Layout.fillWidth: true; text: root.machine.ssh || "-"; color: Theme.textSecondary; font.family: Theme.monoFamily; font.pixelSize: Theme.fontMeta; elide: Text.ElideRight }

            Text { text: qsTr("Criada"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
            Text { Layout.fillWidth: true; text: root.machine.createdAt || "-"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta; elide: Text.ElideRight }

            Text { text: qsTr("Último boot"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
            Text { Layout.fillWidth: true; text: root.machine.lastBootAt || "-"; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta; elide: Text.ElideRight }

            Text { text: qsTr("Rede isolada"); color: Theme.textTertiary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMicro }
            Text { Layout.fillWidth: true; text: root.machine.isolateNetwork ? qsTr("sim") : qsTr("não"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
        }

        AppCheck {
            id: forwardAgentCheck

            Layout.fillWidth: true
            text: qsTr("Encaminhar SSH agent")
            checked: root.machine.forwardSshAgent === true
            enabled: root.machineId.length > 0
                     && root.machineState !== "provisioning"
                     && root.machineState !== "deleting"
                     && root.machineState !== "deleted"
            onToggled: {
                const current = root.machine.forwardSshAgent === true
                if (checked === current || root.machineId.length === 0) {
                    return
                }
                backend.machines.update({ "id": root.machineId,
                                        "forwardSshAgent": checked })
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Aviso: com o agent encaminhado, processos no guest podem usar as chaves do seu SSH agent do host. Ative apenas em machines confiáveis.")
            color: root.machine.forwardSshAgent ? Theme.warning : Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            visible: root.machine.error !== undefined && String(root.machine.error).length > 0
            text: qsTr("Erro: ") + String(root.machine.error || "")
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        Text {
            text: qsTr("Portas")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            font.weight: Font.DemiBold
        }

        Text {
            Layout.fillWidth: true
            text: root.portsText()
            color: Theme.textSecondary
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        Text {
            text: qsTr("Serial")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            font.weight: Font.DemiBold
        }

        Text {
            Layout.fillWidth: true
            text: root.serialText()
            color: Theme.textSecondary
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        Text {
            text: qsTr("USB")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            font.weight: Font.DemiBold
        }

        Text {
            Layout.fillWidth: true
            text: root.usbText()
            color: Theme.textSecondary
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        Text {
            text: qsTr("Mounts")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            font.weight: Font.DemiBold
        }

        Text {
            Layout.fillWidth: true
            text: root.mountsText()
            color: Theme.textSecondary
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }
    }
}
