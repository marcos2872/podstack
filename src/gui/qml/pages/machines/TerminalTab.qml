import QtQuick
import QtQuick.Layouts
import PodStack.Design

// Aba "Terminal" do inspetor de machine — o C++ sobrepõe a sessão SSH sobre
// `placeholder`. O hospedeiro consulta a área para posicionar o terminal.
Item {
    id: root

    property var machine: ({})
    property string machineId: ""
    property string terminalError: ""

    /// Item onde o C++ sobrepõe o TerminalView da sessão SSH.
    readonly property alias placeholder: terminalPlaceholder

    signal externalTerminalRequested()
    signal areaChanged()

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        anchors.topMargin: 8
        anchors.bottomMargin: 14
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: root.machine.ssh
                      ? qsTr("Sessão SSH — %1").arg(String(root.machine.ssh))
                      : qsTr("Sessão SSH")
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
                enabled: root.machineId.length > 0
                onClicked: root.externalTerminalRequested()
                Accessible.name: qsTr("Abrir uma nova sessão SSH em janela")
            }
        }

        Item {
            id: terminalPlaceholder
            Layout.fillWidth: true
            Layout.fillHeight: true
            onWidthChanged: root.areaChanged()
            onHeightChanged: root.areaChanged()

            Text {
                anchors.centerIn: parent
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                visible: root.terminalError.length > 0
                text: root.terminalError
                color: Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }
    }
}
