import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Diálogo de clonagem de machine.
AppModal {
    id: root

    property string pendingId: ""
    /// Função do hospedeiro que traduz o estágio do job em texto.
    property var archiveStageLabel: function(stage) { return stage }

    function openFor(id) {
        root.pendingId = id
        cloneName.text = ""
        backend.machines.resetCreateState()
        root.open()
    }

    title: qsTr("Clonar machine")
    modal: true
    width: 520
    anchors.centerIn: parent
    closePolicy: backend.machines.createActive
                 ? Popup.NoAutoClose
                 : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onClosed: root.pendingId = ""
    background: Rectangle {
        color: Theme.bgSurface
        radius: Theme.radiusLg
        border.width: 1
        border.color: Theme.border
    }

    Connections {
        target: backend

        function onMachineCreateStateChanged() {
            if (backend.machines.createActive || backend.machines.createStage !== "done") {
                return
            }
            if (backend.machines.jobKind === "clone") {
                root.close()
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        Text {
            text: qsTr("Nome da nova machine")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMeta
        }
        AppInput {
            id: cloneName
            Layout.fillWidth: true
            placeholderText: qsTr("copia-da-machine")
            enabled: !backend.machines.createActive
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: backend.machines.createActive || backend.machines.createError.length > 0
                     || backend.machines.createStage === "cancelled"
                     || backend.machines.createStage === "done"

            Text {
                Layout.fillWidth: true
                text: root.archiveStageLabel(backend.machines.createStage)
                color: backend.machines.createError.length > 0 ? Theme.danger
                                                               : Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                elide: Text.ElideRight
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 6
                radius: 3
                color: Theme.bgSurface2

                Rectangle {
                    width: parent.width
                           * Math.max(0, Math.min(1, backend.machines.createProgress / 100))
                    height: parent.height
                    radius: parent.radius
                    color: Theme.indigo500
                }
            }

            Text {
                Layout.fillWidth: true
                visible: backend.machines.createError.length > 0
                text: backend.machines.createError
                color: Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }
    }

    footer: AppFooter {
        Item { Layout.fillWidth: true }
        AppButton {
            text: backend.machines.createActive ? qsTr("Cancelar clone") : qsTr("Fechar")
            variant: "secondary"
            onClicked: {
                if (backend.machines.createActive) {
                    backend.machines.cancelCreate()
                } else {
                    root.close()
                }
            }
        }
        AppButton {
            text: qsTr("Clonar")
            variant: "primary"
            enabled: !backend.machines.createActive && cloneName.text.trim().length > 0
                     && root.pendingId.length > 0
                     && backend.machines.createStage !== "done"
            onClicked: backend.machines.clone(root.pendingId,
                                              cloneName.text.trim())
        }
    }
}
