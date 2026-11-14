import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Aba "Terminal" do detalhe do container — o QTermWidget é sobreposto pelo
// C++ (overlay QWidget) sobre `placeholder`. O hospedeiro consulta a área para
// posicionar a sessão embutida.
Item {
    id: root

    property string containerId: ""

    /// Item onde o C++ sobrepõe o QTermWidget da sessão embutida.
    readonly property alias placeholder: terminalPlaceholder

    signal externalTerminalRequested()
    signal areaChanged()

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.topMargin: 4
        anchors.bottomMargin: 16
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: qsTr("Sessão embutida — podman exec -it %1 /bin/sh")
                          .arg(root.containerId)
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
                elide: Text.ElideRight
            }

            AppButton {
                text: qsTr("Nova sessão em janela")
                iconName: "external"
                size: "sm"
                variant: "secondary"
                enabled: root.containerId.length > 0
                onClicked: root.externalTerminalRequested()
                Accessible.name: qsTr("Abrir uma nova sessão em janela")
            }
        }

        Item {
            id: terminalPlaceholder
            Layout.fillWidth: true
            Layout.fillHeight: true
            onWidthChanged: root.areaChanged()
            onHeightChanged: root.areaChanged()
        }
    }
}
