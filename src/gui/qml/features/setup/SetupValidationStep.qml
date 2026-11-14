import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Etapa 6 — validação final: obrigatórios e avisos.
ScrollView {
    id: root

    property bool validated: false
    property bool jobActive: false
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
            title: qsTr("Validação")
            subtitle: qsTr("Checklist final: itens obrigatórios e avisos. "
                           + "Avisos não bloqueiam; os obrigatórios precisam estar OK.")
        }

        Rectangle {
            Layout.fillWidth: true
            visible: root.validated
            implicitHeight: validationText.implicitHeight + 20
            radius: Theme.radiusSm
            color: (backend.setup.summary.ready === true) ? Theme.successSoft : Theme.warningSoft
            border.width: 1
            border.color: Qt.rgba((backend.setup.summary.ready === true ? Theme.success
                                                                        : Theme.warning).r,
                                  (backend.setup.summary.ready === true ? Theme.success
                                                                        : Theme.warning).g,
                                  (backend.setup.summary.ready === true ? Theme.success
                                                                        : Theme.warning).b,
                                  0.35)

            Text {
                id: validationText
                anchors.fill: parent
                anchors.margins: 10
                text: (backend.setup.summary.ready === true)
                      ? qsTr("Tudo pronto para abrir o PodStack.")
                      : qsTr("Alguns itens precisam de atenção. Você pode corrigir ou continuar mesmo assim.")
                color: (backend.setup.summary.ready === true) ? Theme.success : Theme.warning
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
        }

        Repeater {
            model: root.checks
            delegate: SetupCheckRow {
                Layout.fillWidth: true
                check: modelData
            }
        }

        AppButton {
            text: qsTr("Validar instalação")
            iconName: "shield"
            variant: "primary"
            enabled: !backend.setup.busy && !root.jobActive
            onClicked: backend.setup.validate()
        }

        Item { Layout.preferredHeight: 28 }
    }
}
