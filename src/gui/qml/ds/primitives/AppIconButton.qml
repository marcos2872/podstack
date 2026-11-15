import QtQuick
import QtQuick.Controls
import PodStack.Design

// Botão somente-ícone sobre AppButton.
AppButton {
    id: control

    implicitWidth: implicitHeight
    implicitHeight: compact ? Theme.controlHeightSm : Theme.controlHeight
    Accessible.name: iconName.length > 0 ? iconName : text
}
