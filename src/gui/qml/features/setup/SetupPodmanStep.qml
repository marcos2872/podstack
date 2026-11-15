import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Etapa 3 — Podman: instalar/atualizar, ativar o socket e (opcionalmente)
// remover o Docker. A remoção é confirmada no diálogo do hospedeiro.
ScrollView {
    id: root

    property bool dockerPresent: false
    property bool podmanReady: false
    property bool jobActive: false
    property string podmanAction: ""
    property var podmanCheck: ({})
    property var dockerCheck: ({})

    signal removeDockerRequested()

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
            title: qsTr("Podman")
            subtitle: qsTr("Garanta o Podman instalado, atualizado e rootless. O Docker não é removido por padrão.")
        }

        SetupCard {
            Layout.fillWidth: true
            title: root.dockerPresent ? qsTr("Docker detectado") : qsTr("Docker")
            Text {
                Layout.fillWidth: true
                text: root.dockerPresent
                      ? qsTr("O Docker e o Podman usam sockets diferentes e podem coexistir. "
                             + "Remover o Docker é opcional e destrutivo para containers, imagens, "
                             + "redes e volumes do Docker.")
                      : qsTr("Docker não instalado — nenhum conflito com o Podman.")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            AppButton {
                visible: root.dockerPresent
                text: qsTr("Remover Docker…")
                iconName: "trash"
                variant: "danger"
                enabled: !root.jobActive
                onClicked: root.removeDockerRequested()
            }
        }

        Repeater {
            model: [root.podmanCheck, root.dockerCheck]
            delegate: SetupCheckRow {
                Layout.fillWidth: true
                check: modelData
            }
        }

        SetupCard {
            Layout.fillWidth: true
            title: qsTr("Preparar o Podman")

            Text {
                Layout.fillWidth: true
                text: root.podmanAction === "enable-socket"
                      ? qsTr("O Podman já está instalado. Esta ação ativa o "
                             + "podman.socket de usuário, sem precisar do helper "
                             + "privilegiado.")
                      : qsTr("Instala ou atualiza o Podman e ativa o "
                             + "podman.socket de usuário. A ação não altera o Docker.")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            AppButton {
                text: root.podmanReady ? qsTr("Engine pronto")
                                       : root.podmanAction === "enable-socket"
                                         ? qsTr("Ativar podman.socket")
                                         : qsTr("Instalar Podman")
                iconName: root.podmanReady ? "check" : "download"
                variant: "primary"
                enabled: root.podmanAction.length > 0 && !root.jobActive
                onClicked: backend.setup.apply(root.podmanAction)
            }
        }

        Item { Layout.preferredHeight: 24 }
    }
}
