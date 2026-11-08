import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack 1.0

ApplicationWindow {
    id: window
    width: 1240
    height: 780
    minimumWidth: 960
    minimumHeight: 600
    visible: true
    title: "PodStack"
    color: theme.bgBase

    Theme { id: theme }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Top bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: theme.bgSurface

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12

                Rectangle {
                    width: 34
                    height: 22
                    radius: 11
                    color: theme.indigo700

                    Rectangle {
                        x: 8
                        y: 6
                        width: 8
                        height: 10
                        radius: 2
                        color: theme.cyan
                    }
                    Rectangle {
                        x: 19
                        y: 6
                        width: 8
                        height: 10
                        radius: 2
                        color: theme.indigo500
                    }
                }

                Label {
                    text: "PodStack"
                    color: theme.textPrimary
                    font.pixelSize: 18
                    font.bold: true
                }

                Label {
                    text: "v" + backend.version
                    color: theme.textSecondary
                    font.pixelSize: 11
                }

                Item { Layout.fillWidth: true }

                TextField {
                    id: search
                    Layout.preferredWidth: 280
                    placeholderText: "Buscar containers..."
                    placeholderTextColor: theme.textSecondary
                    color: theme.textPrimary
                    font.pixelSize: 13
                    background: Rectangle {
                        color: theme.bgElevated
                        radius: theme.radius
                        border.color: theme.border
                        border.width: 1
                    }
                }

                Button {
                    id: refreshButton
                    text: "Atualizar"
                    onClicked: backend.refresh()
                    background: Rectangle {
                        color: theme.indigo500
                        radius: theme.radius
                    }
                    contentItem: Text {
                        text: refreshButton.text
                        color: "#FFFFFF"
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        // Error banner
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: backend.error.length > 0 ? 34 : 0
            visible: backend.error.length > 0
            color: theme.danger

            Label {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                verticalAlignment: Text.AlignVCenter
                text: backend.error
                color: "#FFFFFF"
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Sidebar
            Rectangle {
                SplitView.preferredWidth: 220
                SplitView.minimumWidth: 180
                color: theme.bgSurface

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 2

                    Label {
                        text: "DOCKER"
                        color: theme.textSecondary
                        font.pixelSize: 10
                        font.bold: true
                        Layout.leftMargin: 8
                        Layout.topMargin: 8
                        Layout.bottomMargin: 4
                    }

                    Repeater {
                        model: ["Containers", "Images", "Volumes", "Builds", "Networks"]
                        delegate: ItemDelegate {
                            Layout.fillWidth: true
                            text: modelData
                            highlighted: index === 0
                            background: Rectangle {
                                color: parent.highlighted ? theme.bgElevated : "transparent"
                                radius: theme.radius
                            }
                            contentItem: Text {
                                text: parent.text
                                color: parent.highlighted ? theme.textPrimary : theme.textSecondary
                                font.pixelSize: 13
                                leftPadding: 8
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    Label {
                        text: "KUBERNETES"
                        color: theme.textSecondary
                        font.pixelSize: 10
                        font.bold: true
                        Layout.leftMargin: 8
                        Layout.topMargin: 14
                        Layout.bottomMargin: 4
                    }

                    Repeater {
                        model: ["Pods", "Services"]
                        delegate: ItemDelegate {
                            Layout.fillWidth: true
                            text: modelData
                            background: Rectangle {
                                color: "transparent"
                                radius: theme.radius
                            }
                            contentItem: Text {
                                text: parent.text
                                color: theme.textSecondary
                                font.pixelSize: 13
                                leftPadding: 8
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    Label {
                        text: "LINUX"
                        color: theme.textSecondary
                        font.pixelSize: 10
                        font.bold: true
                        Layout.leftMargin: 8
                        Layout.topMargin: 14
                        Layout.bottomMargin: 4
                    }

                    Repeater {
                        model: ["Machines", "Devices"]
                        delegate: ItemDelegate {
                            Layout.fillWidth: true
                            text: modelData
                            background: Rectangle {
                                color: "transparent"
                                radius: theme.radius
                            }
                            contentItem: Text {
                                text: parent.text
                                color: theme.textSecondary
                                font.pixelSize: 13
                                leftPadding: 8
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    ItemDelegate {
                        Layout.fillWidth: true
                        text: "Settings"
                        background: Rectangle {
                            color: "transparent"
                            radius: theme.radius
                        }
                        contentItem: Text {
                            text: parent.text
                            color: theme.textSecondary
                            font.pixelSize: 13
                            leftPadding: 8
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            // Container list
            Rectangle {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 420
                color: theme.bgBase

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

                    ListView {
                        id: containerList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: ContainerListModel { id: containerModel }
                        ScrollBar.vertical: ScrollBar {}

                        delegate: ItemDelegate {
                            width: ListView.view.width
                            height: 56
                            onClicked: containerList.currentIndex = index
                            background: Rectangle {
                                color: containerList.currentIndex === index ? theme.bgElevated
                                                                           : "transparent"
                            }
                            contentItem: RowLayout {
                                spacing: 12

                                Rectangle {
                                    width: 8
                                    height: 8
                                    radius: 4
                                    color: model.state === "running" ? theme.success
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
                                    text: model.state
                                    color: model.state === "running" ? theme.success
                                                                     : theme.textSecondary
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.margins: 24
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
            }

            // Inspector
            Rectangle {
                SplitView.preferredWidth: 340
                SplitView.minimumWidth: 280
                color: theme.bgSurface

                ColumnLayout {
                    id: inspectorLayout
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    property var selected: containerList.currentIndex >= 0
                                           ? containerModel.get(containerList.currentIndex)
                                           : null

                    Label {
                        text: inspectorLayout.selected ? inspectorLayout.selected.name : "Nenhum container selecionado"
                        color: theme.textPrimary
                        font.pixelSize: 16
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Label {
                        text: inspectorLayout.selected ? inspectorLayout.selected.image : ""
                        color: theme.textSecondary
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: theme.border
                    }

                    Label {
                        text: "ID"
                        color: theme.textSecondary
                        font.pixelSize: 10
                        font.bold: true
                    }

                    Label {
                        text: inspectorLayout.selected ? inspectorLayout.selected.id : "—"
                        color: theme.textPrimary
                        font.pixelSize: 11
                        wrapMode: Text.WrapAnywhere
                        Layout.fillWidth: true
                    }

                    Label {
                        text: "PORTAS"
                        color: theme.textSecondary
                        font.pixelSize: 10
                        font.bold: true
                    }

                    Label {
                        text: inspectorLayout.selected && inspectorLayout.selected.ports.length > 0
                              ? inspectorLayout.selected.ports : "—"
                        color: theme.textPrimary
                        font.pixelSize: 11
                        wrapMode: Text.WrapAnywhere
                        Layout.fillWidth: true
                    }

                    Label {
                        text: "ESTADO"
                        color: theme.textSecondary
                        font.pixelSize: 10
                        font.bold: true
                    }

                    Label {
                        text: inspectorLayout.selected ? inspectorLayout.selected.state : "—"
                        color: theme.textPrimary
                        font.pixelSize: 11
                    }

                    Label {
                        text: "AÇÕES"
                        color: theme.textSecondary
                        font.pixelSize: 10
                        font.bold: true
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 6
                        rowSpacing: 6

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Iniciar"
                            enabled: inspectorLayout.selected !== null
                            onClicked: backend.lifecycle("start", inspectorLayout.selected.id)
                        }
                        ActionButton {
                            Layout.fillWidth: true
                            text: "Parar"
                            enabled: inspectorLayout.selected !== null
                            onClicked: backend.lifecycle("stop", inspectorLayout.selected.id)
                        }
                        ActionButton {
                            Layout.fillWidth: true
                            text: "Reiniciar"
                            enabled: inspectorLayout.selected !== null
                            onClicked: backend.lifecycle("restart", inspectorLayout.selected.id)
                        }
                        ActionButton {
                            Layout.fillWidth: true
                            text: "Pausar"
                            enabled: inspectorLayout.selected !== null
                            onClicked: backend.lifecycle("pause", inspectorLayout.selected.id)
                        }
                        ActionButton {
                            Layout.fillWidth: true
                            text: "Despausar"
                            enabled: inspectorLayout.selected !== null
                            onClicked: backend.lifecycle("unpause", inspectorLayout.selected.id)
                        }
                        ActionButton {
                            Layout.fillWidth: true
                            text: "Matar"
                            accentColor: theme.warning
                            enabled: inspectorLayout.selected !== null
                            onClicked: backend.lifecycle("kill", inspectorLayout.selected.id)
                        }
                        ActionButton {
                            Layout.fillWidth: true
                            text: "Remover"
                            accentColor: theme.danger
                            enabled: inspectorLayout.selected !== null
                            onClicked: {
                                removeDialog.containerId = inspectorLayout.selected.id
                                removeDialog.open()
                            }
                        }
                    }

                    Label {
                        text: backend.lastError
                        visible: backend.lastError.length > 0
                        color: theme.danger
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Item { Layout.fillHeight: true }

                    Label {
                        text: "Terminal, files e stats chegam no próximo incremento do M1."
                        color: theme.textSecondary
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }
        }

        // Status bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 26
            color: theme.bgSurface

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16

                Label {
                    text: backend.connected ? "podstackd conectado" : "podstackd desconectado"
                    color: backend.connected ? theme.success : theme.danger
                    font.pixelSize: 10
                }

                Item { Layout.fillWidth: true }

                Label {
                    text: "GPL-3.0-or-later"
                    color: theme.textSecondary
                    font.pixelSize: 10
                }
            }
        }
    }

    Dialog {
        id: removeDialog
        property string containerId
        title: "Remover container?"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle {
            color: theme.bgSurface
            radius: 10
            border.color: theme.border
        }
        contentItem: Label {
            text: "O container será removido permanentemente."
            color: theme.textPrimary
            wrapMode: Text.WordWrap
        }
        onAccepted: backend.removeContainer(containerId, true, false)
    }

    Connections {
        target: backend
        function onContainersChanged(items) {
            containerModel.setContainers(items)
        }
    }
}
