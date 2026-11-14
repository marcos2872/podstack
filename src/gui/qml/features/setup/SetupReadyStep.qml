import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Etapa 7 — resumo e abertura do app.
ScrollView {
    id: root

    property var podmanCheck: ({})
    property var caddyCheck: ({})
    property bool kvmReady: false
    property bool podmanReady: false
    property bool dockerPresent: false

    signal reviewRequested()

    contentWidth: availableWidth
    bottomPadding: 0
    clip: true
    ScrollBar.vertical.policy: ScrollBar.AsNeeded

    ColumnLayout {
        width: root.availableWidth - 48
        x: 24
        y: 24
        spacing: 14

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: 56
            implicitHeight: 56
            radius: 28
            color: Theme.successSoft

            Text {
                anchors.centerIn: parent
                text: "✓"
                color: Theme.success
                font.family: Theme.fontFamily
                font.pixelSize: 28
                font.weight: Font.DemiBold
            }
        }

        SetupStepTitle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(560, root.availableWidth - 48)
            centered: true
            title: backend.setup.mandatoryOk ? qsTr("Tudo pronto")
                                            : qsTr("Itens obrigatórios pendentes")
            subtitle: backend.setup.mandatoryOk
                      ? qsTr("Revise o resumo e abra o PodStack.")
                      : qsTr("Volte às etapas anteriores e conclua os itens "
                             + "obrigatórios para abrir o PodStack.")
        }

        GridLayout {
            Layout.alignment: Qt.AlignHCenter
            columns: 2
            columnSpacing: 48
            rowSpacing: 14

            SetupSummaryItem {
                Layout.preferredWidth: 220
                label: "Podman"
                value: root.podmanCheck.status === "ok"
                       ? "v" + (root.podmanCheck.version || "?") + " · rootless"
                       : root.podmanCheck.status === "warn"
                         ? qsTr("socket inativo")
                         : qsTr("não instalado")
            }

            SetupSummaryItem {
                Layout.preferredWidth: 220
                label: "QEMU/KVM"
                value: root.kvmReady ? qsTr("KVM disponível") : qsTr("TCG (lento)")
            }

            SetupSummaryItem {
                Layout.preferredWidth: 220
                label: qsTr("Socket")
                value: root.podmanReady ? qsTr("ativo") : qsTr("inativo")
            }

            SetupSummaryItem {
                Layout.preferredWidth: 220
                label: "Docker"
                value: root.dockerPresent ? qsTr("coexistindo") : qsTr("não instalado")
            }

            SetupSummaryItem {
                Layout.preferredWidth: 220
                label: "Caddy"
                value: (root.caddyCheck.detail || "").indexOf("não instalado") >= 0
                       ? qsTr("opcional") : qsTr("instalado")
            }

            SetupSummaryItem {
                Layout.preferredWidth: 220
                label: "k3s"
                value: qsTr("VM gerenciada no app")
            }
        }

        AppButton {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Rever diagnóstico")
            iconName: "refresh"
            onClicked: root.reviewRequested()
        }

        Item { Layout.preferredHeight: 24 }
    }
}
