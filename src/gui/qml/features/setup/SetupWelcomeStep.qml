import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Etapa 1 — boas-vindas do assistente de primeira execução.
ScrollView {
    id: root

    contentWidth: availableWidth
    bottomPadding: 0
    clip: true
    ScrollBar.vertical.policy: ScrollBar.AsNeeded

    ColumnLayout {
        width: root.availableWidth - 48
        x: 24
        y: 24
        spacing: 14

        Image {
            Layout.alignment: Qt.AlignHCenter
            source: "qrc:/qt/qml/PodStack/assets/podstack-capsule.svg"
            sourceSize: Qt.size(144, 88)
            Layout.preferredWidth: 72
            Layout.preferredHeight: 44
            fillMode: Image.PreserveAspectFit
        }

        SetupStepTitle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(660, root.availableWidth - 48)
            centered: true
            title: qsTr("Vamos preparar sua máquina")
            subtitle: qsTr("O PodStack roda sobre o Podman nativo e usa QEMU/KVM para máquinas Linux. "
                          + "Vamos detectar o que já existe, instalar ou atualizar o Podman e validar "
                          + "cada componente antes de abrir o app. O Docker pode continuar instalado; "
                          + "removê-lo é opcional.")
        }

        GridLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(720, root.availableWidth - 48)
            columns: 2
            columnSpacing: 10
            rowSpacing: 10

            SetupCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 96
                title: qsTr("Podman nativo")
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Engine rootless, socket e CLI, sem depender do Docker.")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                }
            }

            SetupCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 96
                title: qsTr("QEMU/KVM")
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Machines com cloud-init e virtiofs.")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                }
            }

            SetupCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 96
                title: qsTr("Domínios locais")
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Caddy para *.localhost com HTTPS.")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                }
            }

            SetupCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 96
                title: qsTr("Coexistência")
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Docker e Podman usam sockets diferentes; um não bloqueia o outro.")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                }
            }
        }

        Item { Layout.preferredHeight: 24 }
    }
}
