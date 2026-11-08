import QtQuick
import QtQuick.Layouts
import PodStack

// Linha de check do diagnóstico: estado, nome, versão e remediação.
Rectangle {
    id: root

    property var check: ({})
    property bool showRemediation: true

    readonly property string checkState: check.status || ""
    readonly property color stateColor: {
        if (checkState === "ok") return Theme.success
        if (checkState === "warn") return Theme.warning
        if (checkState === "missing") return Theme.danger
        return Theme.textTertiary
    }
    readonly property string stateLabel: {
        if (checkState === "ok") return qsTr("OK")
        if (checkState === "warn") return qsTr("Atenção")
        if (checkState === "missing") return qsTr("Pendente")
        if (checkState === "optional") return qsTr("Opcional")
        return qsTr("—")
    }
    readonly property string title: {
        if (check.id === "podman") return "Podman"
        if (check.id === "docker") return qsTr("Docker Engine")
        if (check.id === "kvm") return "/dev/kvm"
        if (check.id === "qemu") return "QEMU"
        if (check.id === "ovmf") return "OVMF"
        if (check.id === "virtiofsd") return "virtiofsd"
        if (check.id === "systemd-user") return qsTr("systemd (usuário)")
        if (check.id === "sqlite") return "SQLite"
        if (check.id === "caddy") return "Caddy"
        if (check.id === "k3s") return "k3s"
        return check.id || ""
    }

    implicitHeight: content.implicitHeight + 20
    radius: Theme.radiusSm
    color: Theme.bgSurface2
    border.width: 1
    border.color: Theme.border

    RowLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 10

        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 5
            implicitWidth: 8
            implicitHeight: 8
            radius: 4
            color: root.stateColor
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: root.title
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.DemiBold
                }

                Text {
                    visible: (root.check.version || "").length > 0
                    text: root.check.version
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.check.detail || ""
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                visible: root.showRemediation && root.checkState !== "ok"
                         && (root.check.remediation || "").length > 0
                text: root.check.remediation
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: badge.implicitWidth + 14
            implicitHeight: 20
            radius: 10
            color: Qt.rgba(root.stateColor.r, root.stateColor.g, root.stateColor.b, 0.12)

            Text {
                id: badge
                anchors.centerIn: parent
                text: root.stateLabel
                color: root.stateColor
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                font.weight: Font.DemiBold
            }
        }
    }
}
