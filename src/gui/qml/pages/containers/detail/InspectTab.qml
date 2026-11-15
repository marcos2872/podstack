import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Aba "Inspect" do detalhe do container: JSON completo do Libpod.
Item {
    id: root

    property string inspectText: ""
    property bool loaded: false

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
                text: qsTr("JSON completo do Libpod (containers.inspect)")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
            }

            AppButton {
                text: qsTr("Copiar")
                iconName: "copy"
                size: "sm"
                variant: "secondary"
                enabled: root.loaded
                onClicked: backend.containerDetail.copyInspect()
                Accessible.name: qsTr("Copiar JSON do inspect")
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
                id: inspectScroll
                anchors.fill: parent
                clip: true

                TextArea {
                    id: inspectArea
                    readOnly: true
                    selectByMouse: true
                    persistentSelection: true
                    textFormat: TextEdit.PlainText
                    text: root.inspectText
                    color: Theme.textSecondary
                    selectionColor: Theme.indigo500
                    selectedTextColor: Theme.textPrimary
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontMeta
                    wrapMode: TextEdit.NoWrap
                    width: Math.max(inspectScroll.availableWidth, implicitWidth)
                    padding: 10
                    background: null
                    Accessible.name: qsTr("JSON do inspect do container")
                }
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 32
                visible: root.inspectText.length === 0
                horizontalAlignment: Text.AlignHCenter
                text: backend.containerDetail.loading ? qsTr("Carregando inspect…")
                                                     : qsTr("Inspect indisponível.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
            }
        }
    }
}
