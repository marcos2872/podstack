import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Etapa 5 — componentes obrigatórios e opcionais.
ScrollView {
    id: root

    property bool podmanReady: false
    property bool caddyInstalled: false
    property bool installCaddy: false
    property bool jobActive: false
    property var sqliteCheck: ({})

    signal installCaddyToggled(bool value)

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
            title: qsTr("Componentes")
            subtitle: qsTr("Obrigatórios já vêm com o app. Opcionais só são instalados se você marcar.")
        }

        SetupCard {
            Layout.fillWidth: true
            title: qsTr("Obrigatórios")

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                AppIcon { name: "check"; size: 13; color: Theme.success }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Podman socket + rootless")
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                }
                Text {
                    text: root.podmanReady ? qsTr("OK") : qsTr("pendente")
                    color: root.podmanReady ? Theme.success : Theme.warning
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    font.weight: Font.DemiBold
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                AppIcon { name: "check"; size: 13; color: Theme.success }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("SQLite (histórico de builds)")
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                }
                Text {
                    text: (root.sqliteCheck.version || "").length > 0
                          ? "v" + root.sqliteCheck.version : qsTr("OK")
                    color: Theme.success
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    font.weight: Font.DemiBold
                }
            }
        }

        SetupCard {
            Layout.fillWidth: true
            title: qsTr("Opcionais")

            AppCheck {
                text: qsTr("Caddy — HTTPS para *.localhost")
                      + (root.caddyInstalled ? qsTr(" (instalado)") : "")
                checked: root.installCaddy || root.caddyInstalled
                enabled: !root.caddyInstalled && !root.jobActive
                onToggled: root.installCaddyToggled(checked)
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("Kubernetes (k3s em VM)")
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                font.weight: Font.Medium
            }

            Text {
                Layout.fillWidth: true
                visible: true
                text: qsTr("O cluster roda em uma VM dedicada e é habilitado depois, "
                           + "na página Kubernetes. A VM e o k3s são criados pelo app; "
                           + "nada é instalado no host por este assistente.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            AppButton {
                visible: !root.caddyInstalled
                text: qsTr("Instalar componentes")
                iconName: "download"
                enabled: !root.jobActive && root.installCaddy
                onClicked: {
                    const components = root.installCaddy ? ["caddy"] : []
                    backend.setup.apply("install-components", components)
                }
            }
        }

        Item { Layout.preferredHeight: 24 }
    }
}
