import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ApplicationWindow {
    id: window
    width: 1240
    height: 780
    minimumWidth: 960
    minimumHeight: 600
    visible: true
    title: "PodStack"
    color: theme.bgBase

    property string section: "containers"

    Theme { id: theme }

    readonly property var sections: [
        { group: "DOCKER", items: [
            { key: "containers", label: "Containers" },
            { key: "images", label: "Images" },
            { key: "volumes", label: "Volumes" },
            { key: "networks", label: "Networks" }
        ] },
        { group: "KUBERNETES", items: [
            { key: "pods", label: "Pods" }
        ] },
        { group: "LINUX", items: [
            { key: "machines", label: "Machines", disabled: true },
            { key: "devices", label: "Devices", disabled: true }
        ] }
    ]

    function refreshCurrent() {
        if (section === "containers") {
            backend.refresh()
        } else if (section === "images") {
            backend.refreshImages()
        } else if (section === "volumes") {
            backend.refreshVolumes()
        } else if (section === "networks") {
            backend.refreshNetworks()
        } else if (section === "pods") {
            backend.refreshPods()
        }
    }

    function sectionIndex() {
        if (section === "images") {
            return 1
        }
        if (section === "volumes") {
            return 2
        }
        if (section === "networks") {
            return 3
        }
        if (section === "pods") {
            return 4
        }
        return 0
    }

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

                ActionButton {
                    text: "Novo"
                    visible: window.section === "containers"
                    onClicked: containersPage.openCreate()
                }

                ActionButton {
                    text: "Start all"
                    visible: window.section === "containers"
                    accentColor: theme.success
                    onClicked: backend.batchAll("start")
                }

                ActionButton {
                    text: "Stop all"
                    visible: window.section === "containers"
                    accentColor: theme.warning
                    onClicked: backend.batchAll("stop")
                }

                Button {
                    id: refreshButton
                    text: "Atualizar"
                    onClicked: window.refreshCurrent()
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
                SplitView.preferredWidth: 200
                SplitView.minimumWidth: 170
                color: theme.bgSurface

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 2

                    Repeater {
                        model: window.sections

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Label {
                                text: modelData.group
                                color: theme.textSecondary
                                font.pixelSize: 10
                                font.bold: true
                                Layout.leftMargin: 8
                                Layout.topMargin: 10
                                Layout.bottomMargin: 4
                            }

                            Repeater {
                                model: modelData.items

                                ItemDelegate {
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    enabled: !modelData.disabled
                                    highlighted: window.section === modelData.key
                                    onClicked: window.section = modelData.key
                                    background: Rectangle {
                                        color: parent.highlighted ? theme.bgElevated
                                                                  : "transparent"
                                        radius: theme.radius
                                    }
                                    contentItem: Text {
                                        text: parent.text
                                        color: parent.highlighted
                                               ? theme.textPrimary : theme.textSecondary
                                        font.pixelSize: 13
                                        leftPadding: 8
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    ItemDelegate {
                        Layout.fillWidth: true
                        text: "Settings"
                        onClicked: settingsDialog.open()
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

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: window.sectionIndex()

                ContainersPage {
                    id: containersPage
                }

                ImagesView {
                    id: imagesPage
                }

                VolumesView {
                    id: volumesPage
                }

                NetworksView {
                    id: networksPage
                }

                PodsView {
                    id: podsPage
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
                    text: "imagens " + (backend.diskUsage.images || "—")
                          + "  ·  volumes " + (backend.diskUsage.volumes || "—")
                          + "  ·  containers " + (backend.diskUsage.containers || "—")
                    color: theme.textSecondary
                    font.pixelSize: 10
                }

                Label {
                    text: "GPL-3.0-or-later"
                    color: theme.textSecondary
                    font.pixelSize: 10
                }
            }
        }
    }

    Dialog {
        id: settingsDialog
        title: "Configurações"
        modal: true
        width: 440
        background: Rectangle {
            color: theme.bgSurface
            radius: 10
            border.color: theme.border
        }
        contentItem: ColumnLayout {
            spacing: 10
            Label {
                text: "Modo do engine"
                color: theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            ComboBox {
                id: engineMode
                model: ["rootless", "rootful"]
                currentIndex: backend.settings().engineMode === "rootful" ? 1 : 0
            }
            Label {
                text: "Alterar o modo exige reiniciar o podstackd:\n"
                      + "systemctl --user restart podstack.service"
                color: theme.textSecondary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                text: backend.lastError
                visible: backend.lastError.length > 0
                color: theme.danger
                font.pixelSize: 10
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: "Cancelar"
                accentColor: theme.bgElevated
                onClicked: settingsDialog.close()
            }
            ActionButton {
                text: "Salvar"
                onClicked: {
                    backend.saveEngineMode(engineMode.currentText)
                    settingsDialog.close()
                }
            }
        }
    }

    Connections {
        target: backend
        function onContainersChanged(items) {
            containersPage.setContainers(items)
        }
    }
}
