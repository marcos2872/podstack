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
    /// SelectionState opcional; quando definido, habilita clique com Ctrl/Shift.
    property var selection: null
    /// Campo usado como identidade estável do item (id, name, workingDir...).
    property string idKey: "id"

    signal activated(int index)

    function itemId(item) {
        return item ? String(item[root.idKey] || "") : ""
    }

    function allIds() {
        const ids = []
        for (let i = 0; i < root.items.length; ++i) {
            ids.push(itemId(root.items[i]))
        }
        return ids
    }

    onItemsChanged: {
        if (currentRow >= items.length) {
            currentRow = -1
        }
        if (selection) {
            selection.prune(allIds())
        }
    }

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
            radius: Theme.radiusLg

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
            objectName: "catalogList"

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            activeFocusOnTab: true
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Catálogo")
            model: root.items
            ScrollBar.vertical: ScrollBar {}

            Keys.onUpPressed: {
                if (currentIndex > 0) {
                    root.currentRow = currentIndex - 1
                    currentIndex -= 1
                    if (root.selection) {
                        root.selection.selectOnly(root.itemId(root.items[root.currentRow]),
                                                  root.currentRow)
                    }
                }
            }
            Keys.onDownPressed: {
                if (currentIndex + 1 < root.items.length) {
                    root.currentRow = currentIndex + 1
                    currentIndex += 1
                    if (root.selection) {
                        root.selection.selectOnly(root.itemId(root.items[root.currentRow]),
                                                  root.currentRow)
                    }
                }
            }
            Keys.onReturnPressed: {
                if (currentIndex >= 0 && currentIndex < root.items.length) {
                    root.currentRow = currentIndex
                    root.activated(currentIndex)
                }
            }
            Keys.onEscapePressed: {
                root.currentRow = -1
                currentIndex = -1
            }

            delegate: Item {
                id: row

                property var rowData: modelData
                readonly property bool hovered: rowHover.hovered
                readonly property string rowId: root.itemId(rowData)

                width: ListView.view.width
                height: 44
                Accessible.name: root.columns.length > 0 && rowData
                                 ? String(rowData[root.columns[0].key] || "") : ""
                Accessible.role: Accessible.ListItem
                Accessible.selected: root.selection
                                     ? root.selection.has(rowId)
                                     : root.currentRow === index

                HoverHandler { id: rowHover }

                Rectangle {
                    anchors.fill: parent
                    color: root.selection
                           ? (root.selection.has(row.rowId)
                              ? Theme.accentSoft
                              : row.hovered
                                ? Qt.rgba(Theme.textPrimary.r, Theme.textPrimary.g,
                                          Theme.textPrimary.b, 0.03)
                                : "transparent")
                           : (root.currentRow === index
                              ? Theme.accentSoft
                              : row.hovered
                                ? Qt.rgba(Theme.textPrimary.r, Theme.textPrimary.g,
                                          Theme.textPrimary.b, 0.03)
                                : "transparent")
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.border
                    visible: index < root.items.length - 1
                }

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

                MouseArea {
                    anchors.fill: parent
                    onClicked: function(mouse) {
                        list.forceActiveFocus()
                        root.currentRow = index
                        if (root.selection) {
                            if (mouse.modifiers & Qt.ControlModifier) {
                                root.selection.toggle(row.rowId, index)
                            } else if (mouse.modifiers & Qt.ShiftModifier) {
                                root.selection.selectRange(root.allIds(), index)
                            } else {
                                root.selection.selectOnly(row.rowId, index)
                            }
                        }
                        root.activated(index)
                    }
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
