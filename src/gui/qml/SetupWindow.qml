import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import PodStack

// Assistente de primeira execução: 7 etapas, detecção read-only e ações
// privilegiadas somente via helper allowlisted (nunca shell na GUI).
Window {
    id: setupWindow

    property int step: 0
    property bool installCaddy: false
    property bool installK3s: false
    property bool forceClose: false

    readonly property var checks: backend.setupChecks
    readonly property bool jobActive: backend.setupJobActive
    readonly property bool podmanReady: podmanCheck.status === "ok"
    readonly property bool dockerPresent: (dockerCheck.detail || "").indexOf("coexistindo") >= 0
    readonly property bool kvmReady: kvmCheck.status === "ok"
    readonly property bool validated: backend.setupStatus === "Tudo pronto."
                                 || backend.setupStatus === "Alguns itens precisam de atenção."

    function checkFor(id) {
        for (let index = 0; index < checks.length; ++index) {
            if (checks[index].id === id) {
                return checks[index]
            }
        }
        return {}
    }

    readonly property var podmanCheck: checkFor("podman")
    readonly property var dockerCheck: checkFor("docker")
    readonly property var kvmCheck: checkFor("kvm")
    readonly property var qemuCheck: checkFor("qemu")
    readonly property var ovmfCheck: checkFor("ovmf")
    readonly property var virtiofsdCheck: checkFor("virtiofsd")
    readonly property var sqliteCheck: checkFor("sqlite")
    readonly property var caddyCheck: checkFor("caddy")
    readonly property var k3sCheck: checkFor("k3s")

    function openSetup(refresh) {
        if (refresh) {
            backend.setupRefresh()
        }
        show()
        raise()
        requestActivate()
    }

    function next() {
        if (step < 6) {
            step += 1
        } else {
            backend.setupComplete()
        }
    }

    width: Math.min(1020, Math.max(760, Screen.desktopAvailableWidth - 60))
    height: Math.min(680, Math.max(520, Screen.desktopAvailableHeight - 60))
    x: Screen.virtualX + Math.round((Screen.desktopAvailableWidth - width) / 2)
    y: Screen.virtualY + Math.round((Screen.desktopAvailableHeight - height) / 2)

    title: qsTr("PodStack Setup")
    color: Theme.bgBase
    visible: false

    onClosing: function(close) {
        if (setupWindow.forceClose || backend.setupCompleted) {
            return
        }
        close.accepted = false
        skipDialog.open()
    }

    component SetupCard: Rectangle {
        id: card
        property string title: ""
        default property alias content: body.data
        implicitHeight: cardLayout.implicitHeight + 28
        radius: Theme.radiusLg
        color: Theme.bgSurface
        border.width: 1
        border.color: Theme.border

        ColumnLayout {
            id: cardLayout
            anchors.fill: parent
            anchors.margins: 14
            spacing: 8

            Text {
                Layout.fillWidth: true
                visible: card.title.length > 0
                text: card.title
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontUi
                font.weight: Font.DemiBold
            }

            ColumnLayout {
                id: body
                Layout.fillWidth: true
                spacing: 8
            }
        }
    }

    component StepTitle: ColumnLayout {
        property string title: ""
        property string subtitle: ""
        property bool centered: false
        Layout.fillWidth: true
        spacing: 2

        Text {
            Layout.fillWidth: true
            horizontalAlignment: parent.centered ? Text.AlignHCenter : Text.AlignLeft
            text: parent.title
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontDisplay
            font.weight: Font.DemiBold
            font.letterSpacing: -0.4
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: parent.centered ? Text.AlignHCenter : Text.AlignLeft
            visible: parent.subtitle.length > 0
            text: parent.subtitle
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            wrapMode: Text.WordWrap
        }
    }

    component SummaryItem: ColumnLayout {
        property string label: ""
        property string value: ""
        Layout.fillWidth: true
        spacing: 1

        Text {
            Layout.fillWidth: true
            text: parent.label
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
        }

        Text {
            Layout.fillWidth: true
            text: parent.value
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
    }

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
                        text: backend.setupCompleted ? qsTr("reconfigurar") : qsTr("primeira execução")
                        color: Theme.indigo400
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        font.weight: Font.DemiBold
                    }
                }

                Item { Layout.fillWidth: true }

                ActionButton {
                    text: ""
                    iconName: "x"
                    size: "sm"
                    variant: "ghost"
                    onClicked: setupWindow.close()
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

                // 1 — Boas-vindas
                ScrollView {
                    id: welcomePage
                    contentWidth: availableWidth
                    clip: true

                    ColumnLayout {
                        width: welcomePage.availableWidth - 48
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

                        StepTitle {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: Math.min(660, welcomePage.availableWidth - 48)
                            centered: true
                            title: qsTr("Vamos preparar sua máquina")
                            subtitle: qsTr("O PodStack roda sobre o Podman nativo e usa QEMU/KVM para máquinas Linux. "
                                          + "Vamos detectar o que já existe, instalar ou atualizar o Podman e validar "
                                          + "cada componente antes de abrir o app. O Docker pode continuar instalado; "
                                          + "removê-lo é opcional.")
                        }

                        GridLayout {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: Math.min(720, welcomePage.availableWidth - 48)
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
                    }
                }

                // 2 — Diagnóstico
                ScrollView {
                    id: diagnosisPage
                    contentWidth: availableWidth
                    clip: true

                    ColumnLayout {
                        width: diagnosisPage.availableWidth - 48
                        x: 24
                        y: 24
                        spacing: 12

                        StepTitle {
                            title: qsTr("Diagnóstico do ambiente")
                            subtitle: qsTr("Tudo aqui é somente leitura. Nada é alterado nesta etapa.")
                        }

                        Repeater {
                            model: setupWindow.checks
                            SetupCheckRow {
                                Layout.fillWidth: true
                                check: modelData
                            }
                        }

                        RowLayout {
                            spacing: 8

                            ActionButton {
                                text: qsTr("Escanear novamente")
                                iconName: "refresh"
                                enabled: !backend.setupBusy && !setupWindow.jobActive
                                onClicked: backend.setupCheck()
                            }

                            Text {
                                visible: backend.setupBusy
                                text: qsTr("Detectando…")
                                color: Theme.textTertiary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMicro
                            }
                        }
                    }
                }

                // 3 — Podman
                ScrollView {
                    id: podmanPage
                    contentWidth: availableWidth
                    clip: true

                    ColumnLayout {
                        width: podmanPage.availableWidth - 48
                        x: 24
                        y: 24
                        spacing: 12

                        StepTitle {
                            title: qsTr("Podman")
                            subtitle: qsTr("Garanta o Podman instalado, atualizado e rootless. O Docker não é removido por padrão.")
                        }

                        SetupCard {
                            Layout.fillWidth: true
                            title: setupWindow.dockerPresent ? qsTr("Docker detectado") : qsTr("Docker")
                            Text {
                                Layout.fillWidth: true
                                text: setupWindow.dockerPresent
                                      ? qsTr("O Docker e o Podman usam sockets diferentes e podem coexistir. "
                                             + "Remover o Docker é opcional e destrutivo para containers, imagens, "
                                             + "redes e volumes do Docker.")
                                      : qsTr("Docker não instalado — nenhum conflito com o Podman.")
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMicro
                                wrapMode: Text.WordWrap
                            }

                            ActionButton {
                                visible: setupWindow.dockerPresent
                                text: qsTr("Remover Docker…")
                                iconName: "trash"
                                variant: "danger"
                                enabled: !setupWindow.jobActive
                                onClicked: removeDockerDialog.open()
                            }
                        }

                        Repeater {
                            model: [setupWindow.podmanCheck, setupWindow.dockerCheck]
                            SetupCheckRow {
                                Layout.fillWidth: true
                                check: modelData
                            }
                        }

                        SetupCard {
                            Layout.fillWidth: true
                            title: qsTr("Preparar o Podman")

                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Instala ou atualiza o Podman e ativa o podman.socket de usuário. "
                                           + "A ação não altera o Docker.")
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMicro
                                wrapMode: Text.WordWrap
                            }

                            ActionButton {
                                text: setupWindow.podmanReady ? qsTr("Engine pronto")
                                                              : setupWindow.podmanCheck.status === "warn"
                                                                ? qsTr("Preparar Podman")
                                                                : qsTr("Instalar Podman")
                                iconName: setupWindow.podmanReady ? "check" : "download"
                                variant: "primary"
                                enabled: !setupWindow.podmanReady && !setupWindow.jobActive
                                onClicked: backend.setupApply(setupWindow.podmanCheck.status === "warn"
                                                              ? "upgrade-podman" : "install-podman")
                            }
                        }
                    }
                }

                // 4 — QEMU/KVM
                ScrollView {
                    id: qemuPage
                    contentWidth: availableWidth
                    clip: true

                    ColumnLayout {
                        width: qemuPage.availableWidth - 48
                        x: 24
                        y: 24
                        spacing: 12

                        StepTitle {
                            title: qsTr("QEMU/KVM")
                            subtitle: qsTr("Necessário para máquinas Linux. Sem /dev/kvm, as machines usam TCG (mais lentas).")
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            visible: !setupWindow.kvmReady
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
                            model: [setupWindow.qemuCheck, setupWindow.ovmfCheck,
                                    setupWindow.virtiofsdCheck, setupWindow.kvmCheck]
                            SetupCheckRow {
                                Layout.fillWidth: true
                                check: modelData
                            }
                        }

                        ActionButton {
                            text: qsTr("Instalar virtualização")
                            iconName: "download"
                            variant: "primary"
                            enabled: !setupWindow.jobActive
                                     && (setupWindow.qemuCheck.status === "missing"
                                         || setupWindow.ovmfCheck.status === "missing"
                                         || setupWindow.virtiofsdCheck.status === "missing")
                            onClicked: backend.setupApply("install-qemu")
                        }
                    }
                }

                // 5 — Componentes
                ScrollView {
                    id: componentsPage
                    contentWidth: availableWidth
                    clip: true

                    ColumnLayout {
                        width: componentsPage.availableWidth - 48
                        x: 24
                        y: 24
                        spacing: 12

                        StepTitle {
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
                                    text: setupWindow.podmanReady ? qsTr("OK") : qsTr("pendente")
                                    color: setupWindow.podmanReady ? Theme.success : Theme.warning
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
                                    text: (setupWindow.sqliteCheck.version || "").length > 0
                                          ? "v" + setupWindow.sqliteCheck.version : qsTr("OK")
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

                            FormCheck {
                                text: qsTr("Caddy — HTTPS para *.localhost")
                                checked: setupWindow.installCaddy
                                onToggled: setupWindow.installCaddy = checked
                            }

                            FormCheck {
                                text: qsTr("k3s — Kubernetes local (disponível no M4)")
                                checked: setupWindow.installK3s
                                enabled: false
                            }

                            ActionButton {
                                text: qsTr("Instalar componentes")
                                iconName: "download"
                                enabled: !setupWindow.jobActive && setupWindow.installCaddy
                                onClicked: {
                                    const components = setupWindow.installCaddy ? ["caddy"] : []
                                    backend.setupApply("install-components", components)
                                }
                            }
                        }
                    }
                }

                // 6 — Validação
                ScrollView {
                    id: validationPage
                    contentWidth: availableWidth
                    clip: true

                    ColumnLayout {
                        width: validationPage.availableWidth - 48
                        x: 24
                        y: 24
                        spacing: 12

                        StepTitle {
                            title: qsTr("Validação")
                            subtitle: qsTr("Checklist final: itens obrigatórios e avisos. Você pode abrir o app mesmo com avisos.")
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            visible: setupWindow.validated
                            implicitHeight: validationText.implicitHeight + 20
                            radius: Theme.radiusSm
                            color: (backend.setupSummary.ready === true) ? Theme.successSoft : Theme.warningSoft
                            border.width: 1
                            border.color: Qt.rgba((backend.setupSummary.ready === true ? Theme.success
                                                                                        : Theme.warning).r,
                                                  (backend.setupSummary.ready === true ? Theme.success
                                                                                       : Theme.warning).g,
                                                  (backend.setupSummary.ready === true ? Theme.success
                                                                                       : Theme.warning).b,
                                                  0.35)

                            Text {
                                id: validationText
                                anchors.fill: parent
                                anchors.margins: 10
                                text: (backend.setupSummary.ready === true)
                                      ? qsTr("Tudo pronto para abrir o PodStack.")
                                      : qsTr("Alguns itens precisam de atenção. Você pode corrigir ou continuar mesmo assim.")
                                color: (backend.setupSummary.ready === true) ? Theme.success : Theme.warning
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap
                            }
                        }

                        Repeater {
                            model: setupWindow.checks
                            SetupCheckRow {
                                Layout.fillWidth: true
                                check: modelData
                            }
                        }

                        ActionButton {
                            text: qsTr("Validar instalação")
                            iconName: "shield"
                            variant: "primary"
                            enabled: !backend.setupBusy && !setupWindow.jobActive
                            onClicked: backend.setupValidate()
                        }
                    }
                }

                // 7 — Pronto
                ScrollView {
                    id: donePage
                    contentWidth: availableWidth
                    clip: true

                    ColumnLayout {
                        width: donePage.availableWidth - 48
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

                        StepTitle {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: Math.min(560, donePage.availableWidth - 48)
                            centered: true
                            title: qsTr("Tudo pronto")
                            subtitle: qsTr("Revise o resumo e abra o PodStack.")
                        }

                        GridLayout {
                            Layout.alignment: Qt.AlignHCenter
                            columns: 2
                            columnSpacing: 48
                            rowSpacing: 14

                            SummaryItem {
                                Layout.preferredWidth: 220
                                label: "Podman"
                                value: setupWindow.podmanCheck.status === "ok"
                                       ? "v" + (setupWindow.podmanCheck.version || "?") + " · rootless"
                                       : setupWindow.podmanCheck.status === "warn"
                                         ? qsTr("socket inativo")
                                         : qsTr("não instalado")
                            }

                            SummaryItem {
                                Layout.preferredWidth: 220
                                label: "QEMU/KVM"
                                value: setupWindow.kvmReady ? qsTr("KVM disponível") : qsTr("TCG (lento)")
                            }

                            SummaryItem {
                                Layout.preferredWidth: 220
                                label: qsTr("Socket")
                                value: setupWindow.podmanReady ? qsTr("ativo") : qsTr("inativo")
                            }

                            SummaryItem {
                                Layout.preferredWidth: 220
                                label: "Docker"
                                value: setupWindow.dockerPresent ? qsTr("coexistindo") : qsTr("não instalado")
                            }

                            SummaryItem {
                                Layout.preferredWidth: 220
                                label: "Caddy"
                                value: (setupWindow.caddyCheck.detail || "").indexOf("não instalado") >= 0
                                       ? qsTr("opcional") : qsTr("instalado")
                            }

                            SummaryItem {
                                Layout.preferredWidth: 220
                                label: "k3s"
                                value: (setupWindow.k3sCheck.detail || "").indexOf("não instalado") >= 0
                                       ? qsTr("opcional") : qsTr("instalado")
                            }
                        }

                        ActionButton {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Rever diagnóstico")
                            iconName: "refresh"
                            onClicked: setupWindow.step = 1
                        }
                    }
                }
            }
        }

        // Progresso e log do job
        Rectangle {
            Layout.fillWidth: true
            visible: setupWindow.jobActive || backend.setupLog.length > 0
            Layout.preferredHeight: visible ? 132 : 0
            color: Theme.bgSurface
            border.width: 0

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.border
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.topMargin: 10
                anchors.bottomMargin: 10
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: backend.setupStatus || qsTr("Trabalhando…")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        elide: Text.ElideRight
                    }

                    Text {
                        text: backend.setupProgress + "%"
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 6
                    radius: 3
                    color: Theme.bgElevated

                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(100, backend.setupProgress)) / 100
                        height: parent.height
                        radius: 3
                        color: Theme.indigo500
                    }
                }

                ListView {
                    id: setupLog
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: backend.setupLog
                    boundsBehavior: Flickable.StopAtBounds

                    onCountChanged: positionViewAtEnd()

                    delegate: Text {
                        required property string modelData
                        width: setupLog.width
                        text: modelData
                        color: Theme.textTertiary
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontMicro
                        elide: Text.ElideRight
                    }
                }
            }
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
                    Layout.fillWidth: true
                    text: backend.setupError.length > 0 ? backend.setupError
                                                        : qsTr("Etapa %1 de 7").arg(setupWindow.step + 1)
                    color: backend.setupError.length > 0 ? Theme.danger : Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    elide: Text.ElideRight
                }

                ActionButton {
                    visible: setupWindow.jobActive
                    text: qsTr("Cancelar")
                    variant: "danger"
                    onClicked: backend.setupCancel()
                }

                ActionButton {
                    visible: setupWindow.step > 0 && !setupWindow.jobActive
                    text: qsTr("Voltar")
                    onClicked: setupWindow.step -= 1
                }

                ActionButton {
                    visible: setupWindow.step < 6
                    text: setupWindow.step === 0 ? qsTr("Começar") : qsTr("Continuar")
                    iconName: setupWindow.step === 0 ? "arrowRight" : ""
                    variant: "primary"
                    enabled: !setupWindow.jobActive
                    onClicked: setupWindow.next()
                }

                ActionButton {
                    visible: setupWindow.step === 6
                    text: qsTr("Abrir PodStack")
                    iconName: "check"
                    variant: "primary"
                    enabled: !setupWindow.jobActive
                    onClicked: backend.setupComplete()
                }
            }
        }
    }

    Connections {
        target: backend

        function onSetupChanged() {
            if (backend.setupCompleted && setupWindow.visible) {
                setupWindow.forceClose = true
                setupWindow.close()
            }
        }
    }

    AppDialog {
        id: removeDockerDialog
        title: qsTr("Remover o Docker?")

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

        footer: AppFooter {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Cancelar")
                onClicked: removeDockerDialog.close()
            }
            ActionButton {
                text: qsTr("Remover Docker")
                variant: "danger"
                onClicked: {
                    removeDockerDialog.close()
                    backend.setupRemoveDocker()
                }
            }
        }
    }

    AppDialog {
        id: skipDialog
        title: qsTr("Continuar sem configurar?")

        Text {
            Layout.fillWidth: true
            text: qsTr("Você pode abrir o PodStack agora e executar a configuração depois em "
                       + "Settings → Executar configuração novamente.")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            wrapMode: Text.WordWrap
        }

        footer: AppFooter {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Voltar ao setup")
                onClicked: skipDialog.close()
            }
            ActionButton {
                text: qsTr("Abrir mesmo assim")
                variant: "primary"
                onClicked: {
                    skipDialog.close()
                    setupWindow.forceClose = true
                    backend.setupSkip()
                    setupWindow.close()
                }
            }
        }
    }
}
