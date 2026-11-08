import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

// Select padrão do redesign (ComboBox com background e popup do tema).
ComboBox {
    id: control

    implicitHeight: 34
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody
    leftPadding: 10
    rightPadding: 34
    padding: 0

    contentItem: Text {
        leftPadding: 10
        rightPadding: 30
        text: control.displayText
        color: Theme.textPrimary
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: AppIcon {
        x: control.width - width - 10
        y: (control.height - height) / 2
        name: "chevron"
        size: 14
        rotation: 90
        color: Theme.textSecondary
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.bgSurface2
        border.width: 1
        border.color: control.activeFocus ? Theme.indigo500 : Theme.border
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 240)
        padding: 4

        background: Rectangle {
            color: Theme.bgElevated
            radius: Theme.radiusSm
            border.width: 1
            border.color: Theme.border
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds

            delegate: ItemDelegate {
                width: control.width - 8
                height: 32
                highlighted: control.highlightedIndex === index

                background: Rectangle {
                    radius: Theme.radiusXs
                    color: parent.highlighted ? Theme.accentSoft
                                              : parent.hovered ? Theme.bgHover : "transparent"
                }

                contentItem: Text {
                    text: modelData
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8
                }
            }
        }
    }
}
