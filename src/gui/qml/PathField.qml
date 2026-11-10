import QtQuick
import QtQuick.Layouts
import PodStack

// Campo de caminho com botão que abre o seletor nativo de arquivos
// (QFileDialog via Backend::choosePath). `mode`: open, save ou directory.
RowLayout {
    id: root

    property alias text: field.text
    property alias placeholderText: field.placeholderText
    property alias readOnly: field.readOnly
    property string title: qsTr("Selecionar arquivo")
    property string filter: qsTr("Todos os arquivos (*)")
    property string mode: "open"
    property string initialPath: ""

    spacing: 6

    FormField {
        id: field
        Layout.fillWidth: true
        Layout.preferredHeight: 30
    }

    ActionButton {
        text: qsTr("Procurar…")
        size: "sm"
        variant: "secondary"
        enabled: root.enabled
        onClicked: {
            const selected = backend.choosePath(root.mode, root.title, root.filter,
                                               field.text.length > 0 ? field.text
                                                                     : root.initialPath)
            if (selected.length > 0) {
                field.text = selected
            }
        }
    }
}
