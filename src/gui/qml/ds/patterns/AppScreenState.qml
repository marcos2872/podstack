import QtQuick
import QtQuick.Layouts
import PodStack.Design

// Estado de tela para listas: carregando, vazio, filtro sem resultado e erro.
// Substitui o bloco copiado em cada página e garante o mesmo visual e a mesma
// ação de recuperação ("Tentar novamente" / "Limpar filtro").
Item {
    id: root

    /// loading | ready | empty | filteredEmpty | error
    property string state: "ready"
    property string emptyTitle: qsTr("Nada aqui ainda")
    property string emptyHint: ""
    property string filteredTitle: qsTr("Nenhum item corresponde")
    property string filteredHint: ""
    property string errorText: ""
    property string iconName: "file"
    property bool retrying: false
    /// Ação extra opcional (ex.: "Reiniciar daemon" quando o daemon caiu).
    property string extraActionText: ""
    property bool extraActionBusy: false
    property bool extraActionEnabled: true

    signal retryRequested()
    signal clearFilterRequested()
    signal extraActionClicked()

    visible: state !== "ready"
    Layout.fillWidth: true
    Layout.fillHeight: true

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 420)
        spacing: 8

        AppSpinner {
            Layout.alignment: Qt.AlignHCenter
            visible: root.state === "loading"
            size: 26
        }

        AppIcon {
            Layout.alignment: Qt.AlignHCenter
            visible: root.state !== "loading"
            name: root.state === "error" ? "alert"
                : root.state === "filteredEmpty" ? "search" : root.iconName
            size: 24
            color: root.state === "error" ? Theme.danger : Theme.textTertiary
        }

        AppText {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            role: "body"
            tone: root.state === "error" ? "danger" : "secondary"
            text: {
                if (root.state === "loading") {
                    return qsTr("Carregando…")
                }
                if (root.state === "error") {
                    return root.errorText.length > 0 ? root.errorText : qsTr("Falha ao carregar.")
                }
                if (root.state === "filteredEmpty") {
                    return root.filteredTitle
                }
                return root.emptyTitle
            }
        }

        AppText {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            role: "meta"
            tone: "muted"
            visible: text.length > 0 && root.state !== "loading"
            text: {
                if (root.state === "error") {
                    return qsTr("Verifique a conexão com o daemon.")
                }
                if (root.state === "filteredEmpty") {
                    return root.filteredHint
                }
                return root.emptyHint
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.spacing
            visible: root.state === "error" || root.state === "filteredEmpty"
                     || root.extraActionText.length > 0

            AppButton {
                visible: root.state === "filteredEmpty"
                text: qsTr("Limpar filtro")
                size: "sm"
                variant: "secondary"
                onClicked: root.clearFilterRequested()
            }

            AppButton {
                visible: root.state === "error"
                text: qsTr("Tentar novamente")
                size: "sm"
                variant: "secondary"
                busy: root.retrying
                busyText: qsTr("Tentando…")
                enabled: !root.retrying
                onClicked: root.retryRequested()
            }

            AppButton {
                visible: root.extraActionText.length > 0
                text: root.extraActionText
                size: "sm"
                variant: "primary"
                busy: root.extraActionBusy
                enabled: root.extraActionEnabled && !root.extraActionBusy
                onClicked: root.extraActionClicked()
            }
        }
    }
}
