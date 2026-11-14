import QtQuick
import QtQuick.Layouts
import PodStack.Design

// Catálogo de machines: lista tabular com as colunas do domínio.
AppCatalogList {
    Layout.preferredWidth: 470
    Layout.minimumWidth: 340
    Layout.fillHeight: true
    emptyIcon: "machines"
    emptyText: qsTr("Nenhuma machine.\nCrie uma machine Linux para começar.")
    idKey: "id"
    columns: [
        { key: "name", label: "NOME", width: 0 },
        { key: "distroLabel", label: "DISTRO", width: 110 },
        { key: "state", label: "ESTADO", width: 90 },
        { key: "ssh", label: "SSH", width: 150, mono: true }
    ]
}
