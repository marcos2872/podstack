import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Item de navegação da sidebar (34 px, seleção accent-soft, sem barra).
ItemDelegate {
    id: root

    property string iconName
    property string label
    property int count: -1
    property bool current: false
    property bool available: true

    implicitHeight: Theme.navItemHeight
    implicitWidth: 200
    topPadding: 0
    bottomPadding: 0
    leftPadding: 10
    rightPadding: 10
    enabled: available
    opacity: available ? 1.0 : 0.42
    hoverEnabled: true
    Accessible.role: Accessible.Button
    Accessible.name: label

    background: Rectangle {
        radius: Theme.radiusSm
        color: root.current
               ? Theme.accentSoft
               : root.hovered
                 ? Qt.rgba(Theme.textPrimary.r, Theme.textPrimary.g, Theme.textPrimary.b, 0.05)
                 : "transparent"
    }

    contentItem: RowLayout {
        spacing: 10

        AppIcon {
            name: root.iconName
            size: 16
            color: root.current ? Theme.indigo400 : Theme.textSecondary
            Layout.alignment: Qt.AlignVCenter
        }

        Text {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: root.label
            color: root.current ? Theme.textPrimary : Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontUi
            font.weight: Font.Medium
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        Rectangle {
            visible: root.count >= 0
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: countLabel.implicitWidth + 10
            implicitHeight: 18
            radius: 999
            color: root.current
                   ? Qt.rgba(Theme.indigo500.r, Theme.indigo500.g, Theme.indigo500.b, 0.20)
                   : Theme.bgSurface2

            Text {
                id: countLabel
                anchors.centerIn: parent
                text: root.count
                color: root.current ? Theme.indigo400 : Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
                font.weight: Font.DemiBold
            }
        }
    }
}
