import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Etapa 2 — diagnóstico do ambiente (somente leitura).
ScrollView {
    id: root

    property var checks: []

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
            title: qsTr("Diagnóstico do ambiente")
            subtitle: qsTr("Tudo aqui é somente leitura. Nada é alterado nesta etapa.")
        }

        Repeater {
            model: root.checks
            delegate: SetupCheckRow {
                Layout.fillWidth: true
                check: modelData
            }
        }

        RowLayout {
            spacing: 8

            AppButton {
                text: qsTr("Escanear novamente")
                iconName: "refresh"
                enabled: !backend.setup.busy && !backend.setup.jobActive
                onClicked: backend.setup.check()
            }

            Text {
                visible: backend.setup.busy
                text: qsTr("Detectando…")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
            }
        }

        Item { Layout.preferredHeight: 24 }
    }
}
