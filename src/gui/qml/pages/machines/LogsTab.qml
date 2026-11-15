import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Aba "Logs" do inspetor de machine: console.log e stream do console serial.
ColumnLayout {
    id: root

    property string machineId: ""

    spacing: 0

    function scrollToEnd() {
        const flick = machineLogScroll.contentItem
        if (flick) {
            flick.contentY = Math.max(0, flick.contentHeight - flick.height)
        }
    }

    Connections {
        target: backend

        function onMachineLogsChanged() {
            Qt.callLater(root.scrollToEnd)
        }

        function onMachineConsoleChanged() {
            if (backend.machineDetail.consoleActive) {
                Qt.callLater(root.scrollToEnd)
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 14
        Layout.rightMargin: 14
        Layout.topMargin: 8
        Layout.bottomMargin: 6
        spacing: 8

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            Text {
                Layout.fillWidth: true
                text: backend.machineDetail.consoleActive
                      ? qsTr("console.log · serial (seguindo)")
                      : qsTr("console.log")
                color: Theme.textTertiary
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontMicro
            }

            Text {
                Layout.fillWidth: true
                visible: backend.machineDetail.logsCapturedAt.length > 0
                         && !backend.machineDetail.consoleActive
                text: qsTr("Atualizado em %1").arg(backend.machineDetail.logsCapturedAt)
                color: Theme.textDisabled
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                visible: backend.machineDetail.consoleError.length > 0
                text: backend.machineDetail.consoleError
                color: Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                elide: Text.ElideRight
            }
        }

        AppButton {
            text: qsTr("Copiar")
            size: "sm"
            iconName: "copy"
            enabled: !backend.machineDetail.consoleActive
                     && backend.machineDetail.logs.length > 0
            onClicked: backend.machineDetail.copyLogs()
        }

        AppButton {
            text: qsTr("Atualizar")
            size: "sm"
            iconName: "refresh"
            enabled: root.machineId.length > 0 && !backend.machineDetail.consoleActive
            onClicked: backend.machineDetail.loadLogs(root.machineId, 200)
        }

        AppButton {
            text: backend.machineDetail.consoleActive
                  ? qsTr("Parar console")
                  : qsTr("Seguir console")
            size: "sm"
            iconName: backend.machineDetail.consoleActive ? "stop" : "terminal"
            variant: backend.machineDetail.consoleActive ? "danger" : "secondary"
            enabled: root.machineId.length > 0
            onClicked: {
                if (backend.machineDetail.consoleActive) {
                    backend.machineDetail.stopConsole()
                } else {
                    backend.machineDetail.followConsole(root.machineId, 65536)
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Theme.border
    }

    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true

        ScrollView {
            id: machineLogScroll
            anchors.fill: parent
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            TextArea {
                id: machineLogArea
                width: machineLogScroll.availableWidth
                text: (backend.machineDetail.consoleActive
                       || backend.machineDetail.consoleText.length > 0)
                      ? backend.machineDetail.consoleText
                      : backend.machineDetail.logs.join("\n")
                color: Theme.textSecondary
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontMeta
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.WrapAnywhere
                readOnly: true
                selectByMouse: true
                selectionColor: Theme.accentSoft
                selectedTextColor: Theme.textPrimary
                padding: 8
                onTextChanged: Qt.callLater(root.scrollToEnd)
            }
        }

        Text {
            anchors.centerIn: parent
            width: parent.width - 40
            horizontalAlignment: Text.AlignHCenter
            visible: backend.machineDetail.logs.length === 0
                     && backend.machineDetail.consoleText.length === 0
            text: backend.machineDetail.consoleActive
                  ? qsTr("Seguindo o console serial. Sem dados ainda.")
                  : qsTr("Sem linhas de console.\nA machine ainda não inicializou ou o log está vazio.")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }
    }
}
