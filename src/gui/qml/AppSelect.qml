import QtQuick
import QtQuick.Controls
import PodStack

// Select padrão do redesign (ComboBox com background e popup do tema).
//
// O delegate fica no ComboBox, não no ListView do popup: quando o popup declara
// o próprio delegate e usa `control.delegateModel` como model, o ComboBox perde
// a resolução do item atual e zera `currentIndex` ao abrir (o texto selecionado
// sumia). O delegate precisa ler `model[control.textRole]` — para arrays de
// strings com textRole vazio isso resolve para o próprio item.
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

    delegate: ItemDelegate {
        id: item
        required property var model
        required property int index

        width: control.width
        height: 32
        text: model[control.textRole]
        highlighted: control.highlightedIndex === index

        background: Rectangle {
            radius: Theme.radiusXs
            color: item.highlighted ? Theme.accentSoft
                                    : item.hovered ? Theme.bgHover : "transparent"
        }

        contentItem: Text {
            text: item.text
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            verticalAlignment: Text.AlignVCenter
            leftPadding: 8
            elide: Text.ElideRight
        }
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
        }
    }
}
