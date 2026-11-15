import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Aba "Ambiente" do detalhe do container. As chaves sensíveis ficam
// mascaradas até o usuário marcar "Mostrar valores" (estado no arquivo
// hospedeiro, que também calcula o texto exibido).
Item {
    id: root

    property int envCount: 0
    property string envText: ""
    property bool showEnvValues: false

    signal showValuesToggled(bool value)

    function copyTextArea(area) {
        if (!area || area.text.length === 0) {
            return
        }
        area.selectAll()
        area.copy()
        area.deselect()
        area.cursorPosition = area.length
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.topMargin: 4
        anchors.bottomMargin: 16
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: qsTr("%1 variáveis de ambiente").arg(root.envCount)
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
            }

            AppCheck {
                id: showEnv
                text: qsTr("Mostrar valores")
                checked: root.showEnvValues
                onToggled: root.showValuesToggled(checked)
            }

            AppButton {
                text: qsTr("Copiar")
                iconName: "copy"
                size: "sm"
                variant: "secondary"
                enabled: root.envCount > 0
                onClicked: root.copyTextArea(envArea)
                Accessible.name: qsTr("Copiar variáveis de ambiente")
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.terminalBg
            radius: Theme.radiusSm
            border.width: 1
            border.color: Theme.border
            clip: true

            ScrollView {
                id: envScroll
                anchors.fill: parent
                clip: true

                TextArea {
                    id: envArea
                    readOnly: true
                    selectByMouse: true
                    persistentSelection: true
                    textFormat: TextEdit.PlainText
                    text: root.envText
                    color: Theme.textSecondary
                    selectionColor: Theme.indigo500
                    selectedTextColor: Theme.textPrimary
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontMeta
                    wrapMode: TextEdit.WrapAnywhere
                    width: envScroll.availableWidth
                    padding: 10
                    background: null
                    Accessible.name: qsTr("Variáveis de ambiente do container")
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Chaves com token, password, secret, authorization ou key "
                       + "ficam mascaradas até marcar “Mostrar valores”.")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }
    }
}
