import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

Item {
    id: containersPage

    property var selectedIds: ({})
    property int anchorIndex: -1

    Theme { id: theme }

    readonly property int selectedCount: Object.keys(selectedIds).length

    function idAt(index) {
        if (index < 0 || index >= containerModel.count) {
            return ""
        }
        return containerModel.get(index).id
    }

    function selectOnly(index) {
        const id = idAt(index)
        if (id === "") {
            return
        }
        const next = {}
        next[id] = true
        selectedIds = next
        anchorIndex = index
    }

    function toggleSelection(index) {
        const id = idAt(index)
        if (id === "") {
            return
        }
        const next = Object.assign({}, selectedIds)
        if (next[id]) {
            delete next[id]
        } else {
            next[id] = true
        }
        selectedIds = next
        anchorIndex = index
    }

    function selectRange(index) {
        if (anchorIndex < 0) {
            selectOnly(index)
            return
        }
        const from = Math.min(anchorIndex, index)
        const to = Math.max(anchorIndex, index)
        const next = {}
        for (let row = from; row <= to; ++row) {
            const id = idAt(row)
            if (id !== "") {
                next[id] = true
            }
        }
        selectedIds = next
    }

    function clearSelection() {
        selectedIds = {}
        anchorIndex = -1
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: theme.bgBase

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 8

                Label {
                    text: "Containers"
                    color: theme.textPrimary
                    font.pixelSize: 15
                    font.bold: true
                }

                Label {
                    text: containerModel.count + " itens"
                    color: theme.textSecondary
                    font.pixelSize: 11
                }

                Item { Layout.fillWidth: true }

                Label {
                    visible: containersPage.selectedCount > 0
                    text: containersPage.selectedCount + " selecionados"
                    color: theme.cyan
                    font.pixelSize: 11
                }

                ActionButton {
                    visible: containersPage.selectedCount > 0
                    text: "Iniciar"
                    accentColor: theme.success
                    onClicked: backend.batchIds(Object.keys(containersPage.selectedIds), "start")
                }

                ActionButton {
                    visible: containersPage.selectedCount > 0
                    text: "Parar"
                    accentColor: theme.warning
                    onClicked: backend.batchIds(Object.keys(containersPage.selectedIds), "stop")
                }

                ActionButton {
                    visible: containersPage.selectedCount > 0
                    text: "Remover"
                    accentColor: theme.danger
                    onClicked: backend.batchIds(Object.keys(containersPage.selectedIds), "remove")
                }

                Label {
                    text: backend.connected ? "conectado" : "offline"
                    color: backend.connected ? theme.success : theme.danger
                    font.pixelSize: 11
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: theme.border
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: containerList
                SplitView.fillWidth: true
                SplitView.minimumWidth: 380
                clip: true
                model: ContainerListModel { id: containerModel }
                ScrollBar.vertical: ScrollBar {}

                delegate: ItemDelegate {
                    width: ListView.view.width
                    height: 56
                    background: Rectangle {
                        color: containersPage.selectedIds[model.id]
                               ? theme.indigo700
                               : (containerList.currentIndex === index ? theme.bgElevated
                                                                       : "transparent")
                    }
                    contentItem: RowLayout {
                        spacing: 12

                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            color: model.state === "running"
                                   ? (model.health === "unhealthy" ? theme.danger
                                      : model.health === "starting" ? theme.warning
                                                                     : theme.success)
                                   : theme.textSecondary
                        }

                        ColumnLayout {
                            spacing: 2
                            Layout.fillWidth: true

                            Label {
                                text: model.name
                                color: theme.textPrimary
                                font.pixelSize: 13
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Label {
                                text: model.image
                                      + (model.ports.length > 0 ? "  ·  " + model.ports : "")
                                color: theme.textSecondary
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        Label {
                            text: (model.status || "").length > 0 ? model.status : model.state
                            color: model.state === "running"
                                   ? (model.health === "unhealthy" ? theme.danger
                                      : model.health === "starting" ? theme.warning
                                                                     : theme.success)
                                   : theme.textSecondary
                            font.pixelSize: 11
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: function(mouse) {
                            containerList.currentIndex = index
                            if (mouse.modifiers & Qt.ControlModifier) {
                                containersPage.toggleSelection(index)
                            } else if (mouse.modifiers & Qt.ShiftModifier) {
                                containersPage.selectRange(index)
                            } else {
                                containersPage.selectOnly(index)
                            }
                        }
                    }
                }

                Label {
                                width: parent.width - 48
                    visible: containerModel.count === 0
                    horizontalAlignment: Text.AlignHCenter
                    text: backend.connected
                          ? "Nenhum container encontrado.\nCrie um container pela CLI: podstack run ..."
                          : "podstackd não está acessível.\nVerifique: systemctl --user status podstack.socket"
                    color: theme.textSecondary
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }

            Inspector {
                SplitView.preferredWidth: 340
                SplitView.minimumWidth: 280
                selected: containerModel.revision >= 0 && containerList.currentIndex >= 0
                          ? containerModel.get(containerList.currentIndex)
                          : null
            }
        }
    }

    Dialog {
        id: createDialog
        title: "Novo container"
        modal: true
        width: 640
        background: Rectangle {
            color: theme.bgSurface
            radius: 10
            border.color: theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            GridLayout {
                columns: 2
                Layout.fillWidth: true
                columnSpacing: 10
                rowSpacing: 6

                Label { text: "Imagem"; color: theme.textSecondary; font.pixelSize: 11 }
                FormField {
                    id: createImage
                    Layout.fillWidth: true
                    placeholderText: "nginx:alpine"
                }

                Label { text: "Nome"; color: theme.textSecondary; font.pixelSize: 11 }
                FormField {
                    id: createName
                    Layout.fillWidth: true
                    placeholderText: "opcional"
                }

                Label { text: "Comando"; color: theme.textSecondary; font.pixelSize: 11 }
                FormField {
                    id: createCommand
                    Layout.fillWidth: true
                    placeholderText: "opcional"
                }

                Label { text: "Portas"; color: theme.textSecondary; font.pixelSize: 11 }
                FormField {
                    id: createPorts
                    Layout.fillWidth: true
                    placeholderText: "8080:80, 127.0.0.1:8081:81"
                }

                Label { text: "Env"; color: theme.textSecondary; font.pixelSize: 11 }
                FormField {
                    id: createEnv
                    Layout.fillWidth: true
                    placeholderText: "A=B, C=D"
                }

                Label { text: "Volumes"; color: theme.textSecondary; font.pixelSize: 11 }
                FormField {
                    id: createMounts
                    Layout.fillWidth: true
                    placeholderText: "/host:/container, /tmp:/mnt"
                }

                Label { text: "Workdir"; color: theme.textSecondary; font.pixelSize: 11 }
                FormField {
                    id: createWorkDir
                    Layout.fillWidth: true
                    placeholderText: "opcional"
                }

                Label { text: "Usuário"; color: theme.textSecondary; font.pixelSize: 11 }
                FormField {
                    id: createUser
                    Layout.fillWidth: true
                    placeholderText: "opcional (uid:gid)"
                }
            }

            RowLayout {
                Layout.fillWidth: true
                FormCheck { id: createRemove; text: "Remover ao parar" }
                FormCheck { id: createInit; text: "Init" }
                FormCheck { id: createPrivileged; text: "Privileged" }
                Item { Layout.fillWidth: true }
            }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: "Cancelar"
                accentColor: theme.bgElevated
                onClicked: createDialog.close()
            }
            ActionButton {
                text: "Criar e iniciar"
                onClicked: {
                    backend.createContainer({
                        image: createImage.text,
                        name: createName.text,
                        command: createCommand.text,
                        portsText: createPorts.text,
                        envText: createEnv.text,
                        mountsText: createMounts.text,
                        workDir: createWorkDir.text,
                        user: createUser.text,
                        remove: createRemove.checked,
                        init: createInit.checked,
                        privileged: createPrivileged.checked
                    }, true)
                    createDialog.close()
                }
            }
        }
    }

    function setContainers(items) {
        containerModel.setContainers(items)

        const next = {}
        let changed = false
        for (const id in selectedIds) {
            let found = false
            for (let row = 0; row < items.length; ++row) {
                if (items[row].id === id) {
                    found = true
                    break
                }
            }
            if (found) {
                next[id] = true
            } else {
                changed = true
            }
        }
        if (changed) {
            selectedIds = next
        }
        if (containerList.currentIndex < 0 && containerModel.count > 0) {
            containerList.currentIndex = 0
        }
    }

    function openCreate() {
        createImage.text = ""
        createName.text = ""
        createCommand.text = ""
        createPorts.text = ""
        createEnv.text = ""
        createMounts.text = ""
        createWorkDir.text = ""
        createUser.text = ""
        createRemove.checked = false
        createInit.checked = false
        createPrivileged.checked = false
        createDialog.open()
    }
}
