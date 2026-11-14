import QtQuick
import PodStack.Design

// Aba "Arquivos" do detalhe do container: file manager via containers RPC.
Item {
    id: root

    property string containerId: ""

    AppFileList {
        anchors.fill: parent
        anchors.margins: 20
        containerId: root.containerId
    }
}
