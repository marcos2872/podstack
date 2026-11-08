import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

// Tabela de catálogo: painel, cabeçalho de colunas, linhas e estado vazio.
Rectangle {
    id: root

    property var items: []
    property var columns: []
    property int currentRow: -1
    property string emptyText: qsTr("Nenhum item.")
    property string emptyIcon: "file"

    signal activated(int index)

    color: Theme.bgSurface
    radius: Theme.radiusLg
    border.width: 1
    border.color: Theme.border
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Cabeçalho
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: Theme.bgSurface2

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 12

                Repeater {
                    model: root.columns

                    Text {
                        Layout.preferredWidth: modelData.width > 0 ? modelData.width : -1
                        Layout.fillWidth: modelData.width <= 0
                        text: modelData.label
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.8
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.border
            }
        }

        ListView {
            id: list

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.items
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: row

                property var rowData: modelData

                width: ListView.view.width
                height: 44
                hoverEnabled: true
                Accessible.name: root.columns.length > 0 && rowData
                                 ? String(rowData[root.columns[0].key] || "") : ""

                background: Rectangle {
                    color: root.currentRow === index
                           ? Theme.accentSoft
                           : row.hovered
                             ? Qt.rgba(Theme.textPrimary.r, Theme.textPrimary.g,
                                       Theme.textPrimary.b, 0.03)
                             : "transparent"

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.border
                        visible: index < root.items.length - 1
                    }
                }

                contentItem: RowLayout {
                    spacing: 12

                    Repeater {
                        model: root.columns

                        Text {
                            Layout.preferredWidth: modelData.width > 0 ? modelData.width : -1
                            Layout.fillWidth: modelData.width <= 0
                            text: {
                                if (!modelData || !modelData.key || !row.rowData) {
                                    return ""
                                }
                                const value = row.rowData[modelData.key]
                                return value === undefined || value === null ? "" : String(value)
                            }
                            color: index === 0 ? Theme.textPrimary : Theme.textSecondary
                            font.family: modelData.mono ? Theme.monoFamily : Theme.fontFamily
                            font.pixelSize: index === 0 ? 12.5 : Theme.fontMeta
                            font.weight: index === 0 ? Font.Medium : Font.Normal
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                onClicked: {
                    root.currentRow = index
                    root.activated(index)
                }
            }

            // Estado vazio
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 420)
                visible: list.count === 0
                spacing: 6

                AppIcon {
                    Layout.alignment: Qt.AlignHCenter
                    name: root.emptyIcon
                    size: 22
                    color: Theme.textTertiary
                }

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: root.emptyText
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
