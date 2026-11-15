import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Etapa 4 — QEMU/KVM: dependências das machines Linux.
ScrollView {
    id: root

    property bool kvmReady: false
    property bool jobActive: false
    property var qemuCheck: ({})
    property var ovmfCheck: ({})
    property var virtiofsdCheck: ({})
    property var kvmCheck: ({})

    contentWidth: availableWidth
    bottomPadding: 0
    clip: true
    ScrollBar.vertical.policy: ScrollBar.AsNeeded

    ColumnLayout {
        width: root.availableWidth - 48
        x: 24
        y: 24
        spacing: 12

        SetupStepTitle {
            title: qsTr("QEMU/KVM")
            subtitle: qsTr("Necessário para máquinas Linux. Sem /dev/kvm, as machines usam TCG (mais lentas).")
        }

        Rectangle {
            Layout.fillWidth: true
            visible: !root.kvmReady
            implicitHeight: tcgText.implicitHeight + 20
            radius: Theme.radiusSm
            color: Theme.warningSoft
            border.width: 1
            border.color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.35)

            Text {
                id: tcgText
                anchors.fill: parent
                anchors.margins: 10
                text: qsTr("/dev/kvm indisponível. Você pode continuar com TCG, ciente de que as "
                           + "máquinas ficarão bem mais lentas.")
                color: Theme.warning
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }

        Repeater {
            model: [root.qemuCheck, root.ovmfCheck, root.virtiofsdCheck, root.kvmCheck]
            delegate: SetupCheckRow {
                Layout.fillWidth: true
                check: modelData
            }
        }

        AppButton {
            text: qsTr("Instalar virtualização")
            iconName: "download"
            variant: "primary"
            enabled: !root.jobActive
                     && (root.qemuCheck.status === "missing"
                         || root.ovmfCheck.status === "missing"
                         || root.virtiofsdCheck.status === "missing")
            onClicked: backend.setup.apply("install-qemu")
        }

        Item { Layout.preferredHeight: 24 }
    }
}
