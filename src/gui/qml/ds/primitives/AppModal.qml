import QtQuick
import QtQuick.Controls
import PodStack.Design

// Diálogo base do redesign: cantos arredondados, padding, header próprio e
// acompanhamento opcional de uma ação assíncrona.
//
// Com `actionController` definido, `runAction()` liga o diálogo ao
// `actionSettled` do controller: o botão mostra progresso, o diálogo **não**
// fecha na chamada, fecha no sucesso e mantém o erro visível (no header) em
// caso de falha. Sem controller, `runAction()` fecha na hora — comportamento
// dos diálogos puramente locais.
Dialog {
    id: control

    modal: true
    anchors.centerIn: parent
    padding: 0
    leftPadding: 18
    rightPadding: 18
    topPadding: 0
    bottomPadding: 18

    /// Controller dono da ação (ex.: `backend.volumes`). Opcional.
    property var actionController: null
    /// Método acompanhado (ex.: `"volumes.create"`). Definido por `runAction()`.
    property string actionMethod: ""
    property bool busy: false
    property string busyText: qsTr("Aplicando…")
    property string errorText: ""

    readonly property bool awaitsAction: actionController !== null && actionMethod.length > 0

    signal actionFinished(bool ok, string message)

    /// Executa `fn` acompanhando a conclusão quando há controller. Em erro,
    /// mantém o diálogo aberto com o formulário intacto.
    function runAction(fn, method) {
        if (busy) {
            return
        }
        if (method !== undefined && method !== null && method.length > 0) {
            actionMethod = method
        }
        errorText = ""
        if (!awaitsAction) {
            fn()
            close()
            return
        }
        busy = true
        fn()
    }

    Connections {
        target: control.actionController
        enabled: control.awaitsAction

        function onActionSettled(method, ok, message) {
            if (method !== control.actionMethod) {
                return
            }
            control.busy = false
            control.actionFinished(ok, message)
            if (ok) {
                control.errorText = ""
                control.close()
            } else {
                control.errorText = message.length > 0 ? message : qsTr("A ação falhou.")
            }
        }
    }

    background: Rectangle {
        color: Theme.bgSurface
        radius: Theme.radiusLg
        border.width: 1
        border.color: Theme.border
    }

    header: Item {
        implicitHeight: control.errorText.length > 0 ? 80 : 56

        Text {
            id: titleText
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.top: parent.top
            anchors.topMargin: 14
            text: control.title
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: 17
            font.weight: Font.DemiBold
            font.letterSpacing: -0.3
            elide: Text.ElideRight
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.top: titleText.bottom
            anchors.topMargin: 4
            visible: control.errorText.length > 0
            text: control.errorText
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMeta
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
    }
}
