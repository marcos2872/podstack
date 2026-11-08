import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Botão base do redesign: variantes, tamanho e ícone opcional.
Button {
    id: control

    property string variant: "secondary" // primary | secondary | ghost | danger
    property string size: "md"           // md | sm
    property string iconName: ""

    // Compatibilidade com chamadas antigas (accentColor). Se definido,
    // o botão vira um "soft" na cor indicada.
    property color accentColor: "transparent"

    readonly property bool compact: size === "sm"
    readonly property bool iconOnly: text.length === 0 && iconName.length > 0
    readonly property bool hasAccent: accentColor.a > 0

    readonly property color foreground: {
        if (hasAccent) {
            return accentColor
        }
        if (variant === "primary") {
            return "#ffffff"
        }
        if (variant === "danger") {
            return Theme.danger
        }
        return Theme.textPrimary
    }

    readonly property color backgroundColor: {
        if (hasAccent) {
            return Qt.rgba(accentColor.r, accentColor.g, accentColor.b, hovered ? 0.20 : 0.13)
        }
        if (variant === "primary") {
            return hovered ? Qt.lighter(Theme.indigo500, 1.08) : Theme.indigo500
        }
        if (variant === "ghost") {
            return hovered ? Theme.bgSurface2 : "transparent"
        }
        if (variant === "danger") {
            return hovered ? Theme.dangerSoft : "transparent"
        }
        return hovered ? Theme.bgElevated : Theme.bgSurface2
    }

    implicitHeight: compact ? Theme.controlHeightSm : Theme.controlHeight
    implicitWidth: iconOnly
                   ? implicitHeight
                   : Math.max(implicitHeight, contentRow.implicitWidth + (compact ? 18 : 24))
    padding: 0
    opacity: enabled ? 1.0 : 0.45
    font.family: Theme.fontFamily
    font.pixelSize: compact ? Theme.fontMeta : Theme.fontBody
    font.weight: Font.Medium

    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight

        RowLayout {
            id: contentRow
            anchors.centerIn: parent
            spacing: 7

            AppIcon {
                visible: control.iconName.length > 0
                name: control.iconName
                size: control.compact ? 13 : 14
                color: control.foreground
                Layout.alignment: Qt.AlignVCenter
            }

            Text {
                visible: control.text.length > 0
                text: control.text
                color: control.foreground
                font: control.font
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    background: Rectangle {
        radius: control.compact ? Theme.radiusXs : Theme.radiusSm
        color: control.backgroundColor
        border.width: (control.variant === "secondary" || control.variant === "danger")
                      && !control.hasAccent ? 1 : 0
        border.color: control.variant === "danger" ? Qt.rgba(Theme.danger.r, Theme.danger.g,
                                                             Theme.danger.b, 0.35)
                                                   : Theme.border
    }
}
