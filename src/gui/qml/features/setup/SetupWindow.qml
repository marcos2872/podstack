import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import PodStack.Design

// Assistente de primeira execução: 7 etapas, detecção read-only e ações
// privilegiadas somente via helper allowlisted (nunca shell na GUI).
// É um overlay modal dentro da MainWindow: enquanto os checks obrigatórios
// não passarem, o conteúdo do app fica desabilitado atrás dele. Cada etapa
// vive no próprio componente; este arquivo mantém o chrome (cabeçalho, rail,
// rodapé), o estado das etapas e o diálogo de remoção do Docker.
Item {
    id: setupWindow

    property bool manuallyOpen: false
    property bool jobPanelDismissed: false
    property int step: 0
    property bool installCaddy: false

    readonly property var checks: backend.setup.checks
    readonly property var dockerImpact: backend.setup.dockerImpact
    readonly property bool jobActive: backend.setup.jobActive
    readonly property bool podmanReady: podmanCheck.status === "ok"
    readonly property bool dockerPresent: (dockerCheck.detail || "").indexOf("coexistindo") >= 0
    readonly property bool kvmReady: kvmCheck.status === "ok"
    readonly property bool caddyInstalled: caddyCheck.status === "ok"
    readonly property bool validated: backend.setup.mandatoryOk

    // O overlay fecha quando o setup foi concluído ou quando o usuário escolhe
    // continuar mesmo assim (`skip` grava showOnboarding=false e mantém
    // completed=false, exibindo o aviso de configuração pendente na Main).
    // Reconfigurar abre via Settings e pode ser fechado pelo X.
    visible: manuallyOpen || (backend.setup.loaded && backend.setup.pending)

    function checkFor(id) {
        for (let index = 0; index < checks.length; ++index) {
            if (checks[index].id === id) {
                return checks[index]
            }
        }
        return {}
    }

    function dockerImpactText() {
        if (!dockerImpact || Object.keys(dockerImpact).length === 0) {
            return qsTr("Lendo o impacto do Docker…")
        }
        if (!dockerImpact.available) {
            return qsTr("Não foi possível ler o inventário do Docker. A remoção pode apagar "
                        + "containers, imagens, redes e volumes sem que as contagens sejam "
                        + "exibidas.")
        }
        const containers = dockerImpact.containers || {}
        return qsTr("Impacto estimado: %1 containers (%2 em execução), %3 imagens, "
                    + "%4 redes e %5 volumes.")
            .arg(containers.total || 0)
            .arg(containers.running || 0)
            .arg(dockerImpact.images || 0)
            .arg(dockerImpact.networks || 0)
            .arg(dockerImpact.volumes || 0)
    }

    readonly property var podmanCheck: checkFor("podman")
    readonly property string podmanAction: backend.setup.podmanAction
    readonly property var dockerCheck: checkFor("docker")
    readonly property var kvmCheck: checkFor("kvm")
    readonly property var qemuCheck: checkFor("qemu")
    readonly property var ovmfCheck: checkFor("ovmf")
    readonly property var virtiofsdCheck: checkFor("virtiofsd")
    readonly property var sqliteCheck: checkFor("sqlite")
    readonly property var caddyCheck: checkFor("caddy")
    readonly property var k3sCheck: checkFor("k3s")

    function openSetup(refresh) {
        manuallyOpen = true
        jobPanelDismissed = false
        step = 0
        if (refresh) {
            backend.setup.refreshStatus()
        }
        forceActiveFocus()
    }

    function closeOverlay() {
        if (backend.setup.completed) {
            manuallyOpen = false
        }
    }

    function next() {
        if (step < 6) {
            step += 1
        } else {
            backend.setup.complete()
        }
    }

    Rectangle {
        id: overlayBackground
        anchors.fill: parent
        color: Theme.overlay

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: function(wheel) {
                wheel.accepted = true
            }
        }

        Rectangle {
            id: panel
            anchors.fill: parent
            anchors.margins: 10
            radius: Theme.radiusLg
            color: Theme.bgBase
            border.width: 1
            border.color: Theme.border
            clip: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Header
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    color: Theme.bgSidebar
                    border.width: 0
                    border.color: Theme.border

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.border
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 10
                        spacing: 10

                        Image {
                            source: "qrc:/qt/qml/PodStack/assets/podstack-icon.svg"
                            sourceSize: Qt.size(48, 48)
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            fillMode: Image.PreserveAspectFit
                        }

                        Text {
                            text: qsTr("PodStack Setup")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontUi
                            font.weight: Font.DemiBold
                        }

                        Rectangle {
                            implicitWidth: badgeText.implicitWidth + 12
                            implicitHeight: 20
                            radius: 10
                            color: Theme.accentSoft

                            Text {
                                id: badgeText
                                anchors.centerIn: parent
                                text: backend.setup.completed ? qsTr("reconfigurar") : qsTr("primeira execução")
                                color: Theme.indigo400
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMicro
                                font.weight: Font.DemiBold
                            }
                        }

                        Item { Layout.fillWidth: true }

                        AppButton {
                            visible: backend.setup.completed
                            text: ""
                            iconName: "x"
                            size: "sm"
                            variant: "ghost"
                            onClicked: setupWindow.closeOverlay()
                            Accessible.name: qsTr("Fechar")
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    Rectangle {
                        Layout.preferredWidth: 220
                        Layout.fillHeight: true
                        color: Theme.bgSurface2

                        SetupRail {
                            anchors.fill: parent
                            currentStep: setupWindow.step
                        }
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: setupWindow.step

                        SetupWelcomeStep {}

                        SetupDiagnosisStep {
                            checks: setupWindow.checks
                        }

                        SetupPodmanStep {
                            dockerPresent: setupWindow.dockerPresent
                            podmanReady: setupWindow.podmanReady
                            jobActive: setupWindow.jobActive
                            podmanAction: setupWindow.podmanAction
                            podmanCheck: setupWindow.podmanCheck
                            dockerCheck: setupWindow.dockerCheck
                            onRemoveDockerRequested: removeDockerDialog.open()
                        }

                        SetupVirtualizationStep {
                            kvmReady: setupWindow.kvmReady
                            jobActive: setupWindow.jobActive
                            qemuCheck: setupWindow.qemuCheck
                            ovmfCheck: setupWindow.ovmfCheck
                            virtiofsdCheck: setupWindow.virtiofsdCheck
                            kvmCheck: setupWindow.kvmCheck
                        }

                        SetupComponentsStep {
                            podmanReady: setupWindow.podmanReady
                            sqliteCheck: setupWindow.sqliteCheck
                            caddyInstalled: setupWindow.caddyInstalled
                            installCaddy: setupWindow.installCaddy
                            jobActive: setupWindow.jobActive
                            onInstallCaddyToggled: function(value) {
                                setupWindow.installCaddy = value
                            }
                        }

                        SetupValidationStep {
                            validated: setupWindow.validated
                            checks: setupWindow.checks
                            jobActive: setupWindow.jobActive
                        }

                        SetupReadyStep {
                            podmanCheck: setupWindow.podmanCheck
                            caddyCheck: setupWindow.caddyCheck
                            kvmReady: setupWindow.kvmReady
                            podmanReady: setupWindow.podmanReady
                            dockerPresent: setupWindow.dockerPresent
                            onReviewRequested: setupWindow.step = 1
                        }
                    }
                }

                SetupJobPanel {
                    dismissed: setupWindow.jobPanelDismissed
                    onDismissRequested: setupWindow.jobPanelDismissed = true
                }

                // Footer
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    color: Theme.bgSidebar

                    Rectangle {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 1
                        color: Theme.border
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 20
                        anchors.rightMargin: 20
                        spacing: 8

                        Text {
                            readonly property bool gateBlocked: setupWindow.step === 6
                                                                && !backend.setup.completed
                                                                && !backend.setup.mandatoryOk
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            text: backend.setup.error.length > 0 ? backend.setup.error
                                  : gateBlocked ? qsTr("Conclua os itens obrigatórios para abrir o PodStack.")
                                  : qsTr("Etapa %1 de 7").arg(setupWindow.step + 1)
                            color: backend.setup.error.length > 0 ? Theme.danger
                                   : gateBlocked ? Theme.warning : Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMicro
                            elide: Text.ElideRight
                        }

                        AppButton {
                            visible: setupWindow.jobActive
                            Layout.alignment: Qt.AlignVCenter
                            text: qsTr("Cancelar")
                            variant: "danger"
                            onClicked: backend.setup.cancel()
                        }

                        AppButton {
                            visible: setupWindow.step > 0 && !setupWindow.jobActive
                            Layout.alignment: Qt.AlignVCenter
                            text: qsTr("Voltar")
                            onClicked: setupWindow.step -= 1
                        }

                        AppButton {
                            visible: setupWindow.step < 6
                            Layout.alignment: Qt.AlignVCenter
                            text: setupWindow.step === 0 ? qsTr("Começar") : qsTr("Continuar")
                            variant: "primary"
                            enabled: !setupWindow.jobActive
                            onClicked: setupWindow.next()
                        }

                        AppButton {
                            visible: setupWindow.step === 6 && !backend.setup.completed
                                     && !backend.setup.mandatoryOk
                            Layout.alignment: Qt.AlignVCenter
                            text: qsTr("Continuar mesmo assim")
                            variant: "secondary"
                            enabled: !setupWindow.jobActive
                            onClicked: {
                                setupWindow.manuallyOpen = false
                                backend.setup.skip()
                            }
                        }

                        AppButton {
                            visible: setupWindow.step === 6
                            Layout.alignment: Qt.AlignVCenter
                            iconName: "check"
                            variant: "primary"
                            enabled: backend.setup.completed || backend.setup.mandatoryOk
                            text: backend.setup.completed ? qsTr("Fechar") : qsTr("Abrir PodStack")
                            onClicked: {
                                if (backend.setup.completed) {
                                    setupWindow.closeOverlay()
                                } else {
                                    setupWindow.manuallyOpen = false
                                    backend.setup.complete()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: backend.setup
        function onJobChanged() {
            if (backend.setup.jobActive || backend.setup.error.length > 0) {
                setupWindow.jobPanelDismissed = false
            }
        }
    }

    AppModal {
        id: removeDockerDialog
        title: qsTr("Remover o Docker?")
        width: Math.min(520, setupWindow.width - 48)
        onOpened: backend.setup.loadDockerImpact()

        contentItem: ColumnLayout {
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: qsTr("Containers, imagens, redes e volumes do Docker podem ser perdidos. "
                           + "Não há migração automática. O Podman continuará funcionando.\n\n"
                           + "A remoção exige sua autorização no polkit e não pode ser desfeita.")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                text: setupWindow.dockerImpactText()
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                wrapMode: Text.WordWrap
            }
        }

        footer: AppFooter {
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Cancelar")
                onClicked: removeDockerDialog.close()
            }
            AppButton {
                text: qsTr("Remover Docker")
                variant: "danger"
                onClicked: {
                    removeDockerDialog.close()
                    backend.setup.removeDocker()
                }
            }
        }
    }
}
