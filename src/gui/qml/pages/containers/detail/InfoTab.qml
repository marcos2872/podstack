import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Aba "Info" do detalhe do container.
ScrollView {
    id: root

    property var infoFields: []
    property var portLines: []
    property var labelLines: []
    property string imageText: ""
    property string entrypointText: ""
    property string commandText: ""
    property var config: ({})
    property var detail: ({})
    property string containerId: ""

    clip: true
    contentWidth: availableWidth
    ScrollBar.vertical: ScrollBar {}

    ColumnLayout {
        x: 20
        y: 12
        width: Math.max(0, root.availableWidth - 40)
        spacing: 8

        DetailSectionTitle { title: qsTr("Geral") }

        Repeater {
            model: root.infoFields
            delegate: DetailInfoRow {
                label: modelData.label
                value: modelData.value
            }
        }

        DetailSectionTitle {
            Layout.topMargin: 14
            title: qsTr("Imagem e comando")
        }

        DetailInfoRow { label: qsTr("IMAGEM"); value: root.imageText.length > 0 ? root.imageText : "—" }
        DetailInfoRow { label: qsTr("DIGEST"); value: String(root.detail.Image || "—") }
        DetailInfoRow { label: qsTr("ENTRYPOINT"); value: root.entrypointText }
        DetailInfoRow { label: qsTr("COMANDO"); value: root.commandText }
        DetailInfoRow { label: qsTr("WORKDIR"); value: String(root.config.WorkingDir || "—") }
        DetailInfoRow { label: qsTr("USUÁRIO"); value: String(root.config.User || "—") }
        DetailInfoRow { label: qsTr("ID"); value: root.containerId }

        DetailSectionTitle {
            Layout.topMargin: 14
            title: qsTr("Portas publicadas")
        }

        DetailInfoRow {
            visible: root.portLines.length === 0
            label: ""
            value: qsTr("Nenhuma porta publicada.")
        }

        Repeater {
            model: root.portLines
            delegate: DetailInfoRow {
                label: qsTr("PORTA")
                value: modelData
            }
        }

        DetailSectionTitle {
            Layout.topMargin: 14
            title: qsTr("Labels")
        }

        DetailInfoRow {
            visible: root.labelLines.length === 0
            label: ""
            value: qsTr("Nenhuma label definida.")
        }

        Repeater {
            model: root.labelLines
            delegate: Text {
                Layout.fillWidth: true
                text: modelData
                color: Theme.textSecondary
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WrapAnywhere
            }
        }

        Item { Layout.preferredHeight: 16 }
    }
}
