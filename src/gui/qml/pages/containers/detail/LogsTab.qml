import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Aba "Logs" do detalhe do container: stream próprio do containerDetail.
Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.topMargin: 4
        anchors.bottomMargin: 16
        spacing: 6

        Text {
            Layout.fillWidth: true
            visible: backend.containerDetail.logsError.length > 0
            text: backend.containerDetail.logsError
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        AppLogView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            lines: backend.containerDetail.logLines
            emptyText: qsTr("Sem logs. O stream do container será exibido aqui em tempo real.")
            onClearRequested: backend.containerDetail.clearLogs()
        }
    }
}
