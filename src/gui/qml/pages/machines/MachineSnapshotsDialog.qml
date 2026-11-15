import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Diálogo de snapshots do disco qcow2: criar, restaurar e remover.
AppModal {
    id: root

    property string pendingId: ""
    property bool actionPending: false
    property string pendingAction: ""
    property string pendingName: ""

    function openFor(id) {
        root.pendingId = id
        root.actionPending = false
        root.pendingAction = ""
        root.pendingName = ""
        snapshotsName.text = ""
        snapshotsError.text = ""
        backend.machines.refreshSnapshots(id)
        root.open()
    }

    function snapshotSize(value) {
        const number = Number(value || 0)
        if (number <= 0) {
            return "-"
        }
        const units = ["KiB", "MiB", "GiB", "TiB"]
        let scaled = number / 1024
        let index = 0
        while (scaled >= 1024 && index < units.length - 1) {
            scaled /= 1024
            ++index
        }
        return scaled.toFixed(1) + " " + units[index]
    }

    function requestSnapshotAction(action, name) {
        root.pendingAction = action
        root.pendingName = name
        snapshotsError.text = ""
    }

    function confirmSnapshotAction() {
        if (root.pendingId.length === 0 || root.pendingName.length === 0) {
            return
        }
        root.actionPending = true
        snapshotsError.text = ""
        if (root.pendingAction === "restore") {
            backend.machines.restoreSnapshot(root.pendingId, root.pendingName)
        } else if (root.pendingAction === "remove") {
            backend.machines.removeSnapshot(root.pendingId, root.pendingName)
        }
    }

    function createSnapshot() {
        const name = snapshotsName.text.trim()
        if (name.length === 0) {
            snapshotsError.text = qsTr("Informe o nome do snapshot.")
            return
        }
        root.actionPending = true
        root.pendingAction = "create"
        snapshotsError.text = ""
        backend.machines.createSnapshot(root.pendingId, name)
    }

    title: qsTr("Snapshots do disco")
    modal: true
    width: 620
    anchors.centerIn: parent
    closePolicy: root.actionPending ? Popup.NoAutoClose
                                    : Popup.CloseOnEscape
                                      | Popup.CloseOnPressOutside
    onClosed: {
        root.pendingId = ""
        root.actionPending = false
        root.pendingAction = ""
        root.pendingName = ""
        snapshotsError.text = ""
    }
    background: Rectangle {
        color: Theme.bgSurface
        radius: Theme.radiusLg
        border.width: 1
        border.color: Theme.border
    }

    Connections {
        target: backend

        function onActionFinished(ok, message) {
            if (!root.actionPending || !root.opened) {
                return
            }
            const action = root.pendingAction
            root.actionPending = false
            if (ok) {
                root.pendingAction = ""
                root.pendingName = ""
                snapshotsError.text = ""
                if (action === "create") {
                    snapshotsName.text = ""
                }
            } else {
                snapshotsError.text = message.length > 0 ? message : backend.lastError
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 9

        Text {
            Layout.fillWidth: true
            text: qsTr("Snapshots são internos do qcow2. Restaurar substitui o disco atual; exportar ou clonar a machine não os preserva.")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(220, Math.max(44,
                backend.machines.snapshots.length * 38 + 8))
            visible: backend.machines.snapshots.length > 0
            radius: Theme.radiusSm
            color: Theme.bgSurface2
            border.width: 1
            border.color: Theme.border

            ListView {
                id: snapshotsList
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: backend.machines.snapshots
                spacing: 2

                delegate: Item {
                    required property var modelData

                    width: snapshotsList.width
                    height: 38

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 10

                        Text {
                            Layout.preferredWidth: 140
                            Layout.alignment: Qt.AlignVCenter
                            text: modelData.name
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMeta
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }

                        Text {
                            Layout.preferredWidth: 170
                            Layout.alignment: Qt.AlignVCenter
                            text: {
                                const date = new Date(modelData.date)
                                return isNaN(date.getTime())
                                       ? String(modelData.date || "-")
                                       : Qt.formatDateTime(date, "yyyy-MM-dd HH:mm:ss")
                            }
                            color: Theme.textTertiary
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontMicro
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            Layout.preferredWidth: 60
                            Layout.alignment: Qt.AlignVCenter
                            text: root.snapshotSize(modelData.sizeBytes)
                            color: Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontMicro
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                        }

                        AppButton {
                            text: qsTr("Restaurar")
                            size: "sm"
                            variant: "ghost"
                            Layout.alignment: Qt.AlignVCenter
                            enabled: !root.actionPending && !backend.actionActive
                            onClicked: root.requestSnapshotAction("restore",
                                                                  String(modelData.name))
                        }

                        AppButton {
                            text: qsTr("Remover")
                            size: "sm"
                            variant: "danger"
                            Layout.alignment: Qt.AlignVCenter
                            enabled: !root.actionPending && !backend.actionActive
                            onClicked: root.requestSnapshotAction("remove",
                                                                  String(modelData.name))
                        }
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: backend.machines.snapshots.length === 0
            text: qsTr("Nenhum snapshot criado.")
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
        }

        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            text: backend.machines.snapshotsError
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: confirmRow.implicitHeight + 16
            visible: root.pendingAction.length > 0
            radius: Theme.radiusSm
            color: root.pendingAction === "remove" ? Theme.dangerSoft : Theme.warningSoft
            border.width: 1
            border.color: root.pendingAction === "remove" ? Theme.danger : Theme.warning

            RowLayout {
                id: confirmRow
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    text: root.pendingAction === "remove"
                          ? qsTr("Remover o snapshot %1? Esta ação não pode ser desfeita.")
                                .arg(root.pendingName)
                          : qsTr("Restaurar o snapshot %1? O disco atual será substituído e as alterações não salvas serão perdidas.")
                                .arg(root.pendingName)
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMeta
                    wrapMode: Text.WordWrap
                }

                AppButton {
                    text: qsTr("Cancelar")
                    size: "sm"
                    variant: "secondary"
                    Layout.alignment: Qt.AlignVCenter
                    enabled: !root.actionPending
                    onClicked: {
                        root.pendingAction = ""
                        root.pendingName = ""
                    }
                }

                AppButton {
                    text: root.pendingAction === "remove"
                          ? qsTr("Remover") : qsTr("Restaurar")
                    size: "sm"
                    variant: root.pendingAction === "remove" ? "danger" : "primary"
                    Layout.alignment: Qt.AlignVCenter
                    enabled: !root.actionPending
                    onClicked: root.confirmSnapshotAction()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            AppInput {
                id: snapshotsName
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                placeholderText: qsTr("[A-Za-z0-9._-], até 64 caracteres")
                enabled: !root.actionPending
            }

            AppButton {
                text: qsTr("Criar")
                size: "sm"
                variant: "primary"
                Layout.alignment: Qt.AlignVCenter
                enabled: !root.actionPending
                         && !backend.actionActive
                         && root.pendingId.length > 0
                         && snapshotsName.text.trim().length > 0
                onClicked: root.createSnapshot()
            }
        }

        Text {
            id: snapshotsError
            Layout.fillWidth: true
            visible: text.length > 0
            text: ""
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
        }
    }

    footer: AppFooter {
        Item { Layout.fillWidth: true }
        AppButton {
            text: qsTr("Fechar")
            variant: "secondary"
            enabled: !root.actionPending
            onClicked: root.close()
        }
    }
}
