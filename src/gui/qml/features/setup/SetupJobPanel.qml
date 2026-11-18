import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Progresso e log do job do assistente. Some no sucesso; no erro fica com o X.
Rectangle {
    id: root

    property bool dismissed: false

    readonly property bool jobActive: backend.setup.jobActive
    readonly property bool panelVisible: !root.dismissed
                                         && (root.jobActive
                                             || (backend.setup.error.length > 0
                                                 && backend.setup.log.length > 0))

    signal dismissRequested()

    /// O daemon devolve o token cru ("running", "cancelling"); a UI mostra
    /// texto traduzido.
    function statusLabel(status) {
        if (status === "running") {
            return qsTr("Executando…")
        }
        if (status === "queued") {
            return qsTr("Na fila…")
        }
        if (status === "cancelling") {
            return qsTr("Cancelando…")
        }
        if (status === "done") {
            return qsTr("Concluído")
        }
        if (status === "cancelled") {
            return qsTr("Cancelado")
        }
        return qsTr("Trabalhando…")
    }

    Layout.fillWidth: true
    visible: root.panelVisible
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
                text: backend.setup.error.length > 0
                      ? backend.setup.error
                      : (backend.setup.status.length > 0
                         ? root.statusLabel(backend.setup.status)
                         : qsTr("Trabalhando…"))
                color: backend.setup.error.length > 0 ? Theme.danger : Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                elide: Text.ElideRight
            }

            Text {
                text: backend.setup.progress + "%"
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
            }

            AppButton {
                // Enquanto o job roda não há como fechar o log.
                visible: !root.jobActive && backend.setup.error.length > 0
                text: ""
                iconName: "x"
                size: "sm"
                variant: "ghost"
                onClicked: root.dismissRequested()
                Accessible.name: qsTr("Fechar log")
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 6
            radius: 3
            color: Theme.bgElevated

            Rectangle {
                width: parent.width * Math.max(0, Math.min(100, backend.setup.progress)) / 100
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
            model: backend.setup.log
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
