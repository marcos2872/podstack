import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Aba "Volumes" do detalhe do container.
ScrollView {
    id: root

    property var mountEntries: []

    clip: true
    contentWidth: availableWidth
    ScrollBar.vertical: ScrollBar {}

    ColumnLayout {
        x: 20
        y: 12
        width: Math.max(0, root.availableWidth - 40)
        spacing: 10

        Text {
            visible: root.mountEntries.length === 0
            Layout.fillWidth: true
            text: qsTr("Nenhum volume ou bind mount.")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMeta
        }

        Repeater {
            model: root.mountEntries
            delegate: Rectangle {
                id: mountCard

                Layout.fillWidth: true
                implicitHeight: mountColumn.implicitHeight + 24
                radius: Theme.radius
                color: Theme.bgSurface2
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    id: mountColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 5

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: String(modelData.Type || qsTr("mount"))
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            font.weight: Font.DemiBold
                        }

                        AppBadge {
                            text: modelData.RW === false ? qsTr("somente leitura")
                                                         : qsTr("leitura/escrita")
                            tone: modelData.RW === false ? "warning" : "success"
                        }
                    }

                    DetailInfoRow {
                        label: qsTr("NOME")
                        value: String(modelData.Name || "—")
                    }
                    DetailInfoRow {
                        label: qsTr("ORIGEM")
                        value: String(modelData.Source || "—")
                    }
                    DetailInfoRow {
                        label: qsTr("DESTINO")
                        value: String(modelData.Destination || "—")
                    }
                    DetailInfoRow {
                        label: qsTr("DRIVER")
                        value: String(modelData.Driver || "—")
                    }
                    DetailInfoRow {
                        label: qsTr("PROPAGAÇÃO")
                        value: String(modelData.Propagation || "—")
                    }
                }
            }
        }

        Item { Layout.preferredHeight: 16 }
    }
}
