import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Aba "Redes" do detalhe do container.
ScrollView {
    id: root

    property var portLines: []
    property var networkEntries: []

    clip: true
    contentWidth: availableWidth
    ScrollBar.vertical: ScrollBar {}

    ColumnLayout {
        x: 20
        y: 12
        width: Math.max(0, root.availableWidth - 40)
        spacing: 10

        DetailSectionTitle { title: qsTr("Portas publicadas") }

        Repeater {
            model: root.portLines
            delegate: Text {
                Layout.fillWidth: true
                text: modelData
                color: Theme.textSecondary
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontMeta
                wrapMode: Text.WrapAnywhere
            }
        }

        Text {
            visible: root.portLines.length === 0
            Layout.fillWidth: true
            text: qsTr("Nenhuma porta publicada.")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMeta
        }

        DetailSectionTitle {
            Layout.topMargin: 10
            title: qsTr("Interfaces")
        }

        Text {
            visible: root.networkEntries.length === 0
            Layout.fillWidth: true
            text: qsTr("Nenhuma rede conectada.")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMeta
        }

        Repeater {
            model: root.networkEntries
            delegate: Rectangle {
                id: networkCard

                Layout.fillWidth: true
                implicitHeight: networkColumn.implicitHeight + 24
                radius: Theme.radius
                color: Theme.bgSurface2
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    id: networkColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 5

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: modelData.name
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        AppBadge {
                            text: qsTr("rede")
                            tone: "muted"
                        }
                    }

                    DetailInfoRow { label: qsTr("IP"); value: modelData.ip }
                    DetailInfoRow { label: qsTr("GATEWAY"); value: modelData.gateway }
                    DetailInfoRow { label: qsTr("MAC"); value: modelData.mac }
                    DetailInfoRow { label: qsTr("ALIASES"); value: modelData.aliases }
                    DetailInfoRow { label: qsTr("NETWORK ID"); value: modelData.id }
                }
            }
        }

        Item { Layout.preferredHeight: 16 }
    }
}
