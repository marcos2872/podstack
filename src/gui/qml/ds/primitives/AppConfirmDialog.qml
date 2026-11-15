import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Modal de confirmação com ação destrutiva opcional.
//
// Quando `actionController`/`actionMethod` são definidos, o diálogo não fecha
// no clique: entra em `busy`, espera o `actionSettled` do controller e só então
// fecha — ou mantém o erro no próprio formulário. É o padrão para ações longas
// e destrutivas (remoção em lote, prune), que antes fechavam na hora e não
// davam nenhum retorno.
AppModal {
    id: control

    property string message: ""
    property string confirmText: qsTr("Confirmar")
    property string cancelText: qsTr("Cancelar")
    property bool destructive: false

    /// Estado da ação em andamento: spinner no botão e Esc desabilitado.
    property bool busy: false
    property string busyText: qsTr("Aplicando…")
    property string errorText: ""

    /// Controller dono da ação. Se definido, o diálogo espera `actionSettled`.
    property var actionController: null
    property string actionMethod: ""

    signal confirmed()
    signal cancelled()

    readonly property bool awaitsAction: actionController !== null && actionMethod.length > 0

    width: Math.min(parent ? parent.width - 48 : 460, 460)
    closePolicy: busy ? Popup.NoAutoClose : Popup.CloseOnEscape

    onRejected: control.cancelled()

    Connections {
        target: control.actionController
        enabled: control.awaitsAction

        function onActionSettled(method, ok, message) {
            if (method !== control.actionMethod) {
                return
            }
            control.busy = false
            if (ok) {
                control.errorText = ""
                control.close()
            } else {
                control.errorText = message.length > 0 ? message : qsTr("A ação falhou.")
            }
        }
    }

    footer: Item {
        implicitHeight: 56

        RowLayout {
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacing

            AppButton {
                text: control.cancelText
                variant: "secondary"
                enabled: !control.busy
                onClicked: {
                    control.cancelled()
                    control.close()
                }
            }

            AppButton {
                text: control.confirmText
                variant: control.destructive ? "danger" : "primary"
                busy: control.busy
                busyText: control.busyText
                enabled: !control.busy
                onClicked: {
                    if (control.busy) {
                        return
                    }
                    control.errorText = ""
                    control.confirmed()
                    if (control.awaitsAction) {
                        control.busy = true
                    } else {
                        control.close()
                    }
                }
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        Text {
            Layout.fillWidth: true
            text: control.message
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontUi
            wrapMode: Text.Wrap
        }

        Text {
            Layout.fillWidth: true
            visible: control.errorText.length > 0
            text: control.errorText
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMeta
            wrapMode: Text.Wrap
        }
    }
}
