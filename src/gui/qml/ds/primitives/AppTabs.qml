import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Barra de abas segmentada do PodStack.
Item {
    id: root

    property var model: []
    property int currentIndex: 0
    property bool compact: false
    signal activated(int index)

    implicitHeight: 40

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSm
        color: Theme.bgSurface2
        border.width: 1
        border.color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 3
        spacing: 2

        Repeater {
            model: root.model

            ItemDelegate {
                id: tab
                readonly property bool active: root.currentIndex === index

                Layout.fillWidth: true
                implicitHeight: 30
                hoverEnabled: true
                Accessible.name: modelData

                background: Rectangle {
                    radius: Theme.radiusXs
                    color: tab.active ? Theme.bgElevated
                                      : tab.hovered ? Theme.bgHover
                                                    : "transparent"
                }

                contentItem: Text {
                    text: root.compact ? "" : modelData
                    color: tab.active ? Theme.textPrimary : Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMeta
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                onClicked: root.activated(index)
            }
        }
    }
}
