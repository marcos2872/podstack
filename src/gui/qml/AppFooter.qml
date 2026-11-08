import QtQuick
import QtQuick.Layouts

// Rodapé padrão de diálogo: botões alinhados à direita com margens.
Item {
    id: root

    implicitHeight: 56
    default property alias content: row.data

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        anchors.topMargin: 8
        anchors.bottomMargin: 16
        spacing: 8
    }
}
