import QtQuick
import QtQuick.Layouts
import PodStack.Design

// Banner de erro com ação opcional de repetir.
Rectangle {
    id: root

    property string message: ""
    property string actionText: qsTr("Tentar novamente")
    /// `danger` para falhas, `warning` para avisos (ex.: versão divergente).
    property string tone: "danger"
    readonly property color toneColor: tone === "warning" ? Theme.warning : Theme.danger
    readonly property color toneSoft: tone === "warning" ? Theme.warningSoft : Theme.dangerSoft
    signal actionClicked()

    implicitHeight: 40
    radius: Theme.radiusSm
    color: root.toneSoft
    border.width: 1
    border.color: Qt.rgba(root.toneColor.r, root.toneColor.g, root.toneColor.b, 0.35)
    visible: message.length > 0

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingSm
        spacing: Theme.spacingSm

        AppIcon {
            name: "alert"
            size: 14
            color: root.toneColor
        }

        Text {
            Layout.fillWidth: true
            text: root.message
            color: root.toneColor
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            elide: Text.ElideRight
        }

        AppButton {
            visible: root.actionText.length > 0
            text: root.actionText
            variant: "ghost"
            size: "sm"
            onClicked: root.actionClicked()
        }
    }
}
