import QtQuick
import PodStack.Design

// Texto com a escala tipográfica do tema. Evita repetir `font.family`/
// `font.pixelSize` em cada tela.
//
// Uso: AppText { text: qsTr("…") }                       (body, cor primária)
//      AppText { role: "meta"; tone: "muted"; text: … }
Text {
    id: root

    /// body | meta | micro | title | heading
    property string role: "body"
    /// primary | secondary | muted | danger | warning | success | accent
    property string tone: "primary"
    property bool strong: false

    readonly property int roleSize: {
        if (role === "title") {
            return Theme.fontHeading
        }
        if (role === "heading") {
            return Theme.fontHeading - 3
        }
        if (role === "micro") {
            return Theme.fontMicro
        }
        if (role === "meta") {
            return Theme.fontMeta
        }
        return Theme.fontBody
    }

    readonly property color toneColor: {
        if (tone === "secondary") {
            return Theme.textSecondary
        }
        if (tone === "muted") {
            return Theme.textTertiary
        }
        if (tone === "danger") {
            return Theme.danger
        }
        if (tone === "warning") {
            return Theme.warning
        }
        if (tone === "success") {
            return Theme.success
        }
        if (tone === "accent") {
            return Theme.indigo400
        }
        return Theme.textPrimary
    }

    color: toneColor
    font.family: Theme.fontFamily
    font.pixelSize: roleSize
    font.weight: strong ? Font.DemiBold : Font.Normal
    wrapMode: Text.WordWrap
}
