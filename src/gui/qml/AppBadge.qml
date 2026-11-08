import QtQuick
import QtQuick.Layouts

// Pill de status/metadado (POC: .badge).
Rectangle {
    id: root

    property string text
    property string tone: "muted" // success | warning | danger | accent | muted

    readonly property color toneColor: {
        if (tone === "success") {
            return Theme.success
        }
        if (tone === "warning") {
            return Theme.warning
        }
        if (tone === "danger") {
            return Theme.danger
        }
        if (tone === "accent") {
            return Theme.indigo400
        }
        return Theme.textTertiary
    }

    readonly property color toneBackground: {
        if (tone === "success") {
            return Theme.successSoft
        }
        if (tone === "warning") {
            return Theme.warningSoft
        }
        if (tone === "danger") {
            return Theme.dangerSoft
        }
        if (tone === "accent") {
            return Theme.accentSoft
        }
        return Theme.bgSurface2
    }

    implicitWidth: label.implicitWidth + 12
    implicitHeight: 19
    radius: 999
    color: toneBackground
    border.width: tone === "muted" ? 1 : 0
    border.color: Theme.border

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.toneColor
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontMeta
        font.weight: Font.DemiBold
        elide: Text.ElideRight
        width: Math.min(implicitWidth, root.width - 12)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
