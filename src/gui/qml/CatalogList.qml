import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ListView {
    id: root

    property var items: []
    property var columns: []
    property int currentRow: -1

    signal activated(int index)

    clip: true
    model: items
    ScrollBar.vertical: ScrollBar {}

    delegate: ItemDelegate {
        id: item

        property var row: modelData

        width: ListView.view.width
        height: 42
        onClicked: {
            root.currentRow = index
            root.activated(index)
        }

        background: Rectangle {
            color: root.currentRow === index ? theme.bgElevated : "transparent"
        }

        contentItem: RowLayout {
            spacing: 12

            Repeater {
                model: root.columns

                Label {
                    Layout.preferredWidth: modelData.width > 0 ? modelData.width : -1
                    Layout.fillWidth: modelData.width <= 0
                    text: item.row[modelData.key] !== undefined
                          ? String(item.row[modelData.key]) : ""
                    color: index === 0 ? theme.textPrimary : theme.textSecondary
                    font.pixelSize: 11
                    font.bold: index === 0
                    font.family: modelData.mono ? "monospace" : ""
                    elide: Text.ElideMiddle
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    Theme { id: theme }
}
