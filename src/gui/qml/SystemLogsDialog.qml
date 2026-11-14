import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import PodStack

AppDialog {
    id: dialog
    objectName: "systemLogsDialog"
    title: qsTr("Logs do app")
    modal: true
    width: Math.min(1000, window.width - 48)
    height: Math.min(680, window.height - 48)
    anchors.centerIn: parent

    property string levelFilter: ""
    property string componentFilter: ""

    onOpened: backend.loadSystemLogs(levelFilter, componentFilter, 7)

    function openFor(forComponent, forLevel) {
        componentFilter = forComponent || ""
        levelFilter = forLevel || ""
        const componentIndex = componentBox.model.indexOf(componentFilter)
        componentBox.currentIndex = componentIndex > 0 ? componentIndex : 0
        const levelIndex = levelBox.model.indexOf(levelFilter)
        levelBox.currentIndex = levelIndex > 0 ? levelIndex : 0
        open()
        backend.loadSystemLogs(levelFilter, componentFilter, 7)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            AppSelect {
                id: levelBox
                Layout.preferredWidth: 170
                model: [qsTr("Todos os níveis"), "error", "warn", "info", "debug", "trace"]
                onCurrentIndexChanged: {
                    dialog.levelFilter = currentIndex === 0 ? "" : currentText
                    backend.loadSystemLogs(dialog.levelFilter, dialog.componentFilter, 7)
                }
                Accessible.name: qsTr("Filtrar por nível")
            }

            AppSelect {
                id: componentBox
                Layout.preferredWidth: 220
                model: [qsTr("Todos os componentes"), "daemon", "gui", "cli", "qml",
                        "setup", "update", "helper", "package"]
                onCurrentIndexChanged: {
                    dialog.componentFilter = currentIndex === 0 ? "" : currentText
                    backend.loadSystemLogs(dialog.levelFilter, dialog.componentFilter, 7)
                }
                Accessible.name: qsTr("Filtrar por componente")
            }

            Item { Layout.fillWidth: true }

            FormCheck {
                id: followLogs
                text: qsTr("Seguir")
                checked: false
                Accessible.name: qsTr("Seguir logs em tempo real")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            ActionButton {
                text: qsTr("Atualizar")
                size: "sm"
                onClicked: {
                    backend.loadSystemLogs(levelBox.currentIndex === 0 ? "" : levelBox.currentText,
                                           componentBox.currentIndex === 0 ? ""
                                                                          : componentBox.currentText,
                                           7)
                }
            }
            ActionButton {
                text: qsTr("Exportar")
                size: "sm"
                onClicked: exportDialog.open()
            }
            ActionButton {
                text: qsTr("Exportar diagnóstico")
                size: "sm"
                onClicked: backend.generateDiagnosticReport()
            }
            ActionButton {
                text: qsTr("Abrir pasta")
                size: "sm"
                onClicked: backend.openLogsFolder()
            }
            ActionButton {
                text: qsTr("Limpar")
                size: "sm"
                onClicked: clearDialog.open()
            }
            Item { Layout.fillWidth: true }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.bgBase
            radius: Theme.radius
            border.width: 1
            border.color: Theme.border

            ListView {
                id: logList
                anchors.fill: parent
                anchors.margins: 10
                clip: true
                model: backend.systemLogs
                spacing: 2

                delegate: Text {
                    width: logList.width
                    text: (modelData.ts || "") + " [" + (modelData.component || "")
                          + "] " + (modelData.level || "") + ": " + (modelData.message || "")
                    color: modelData.level === "error" ? Theme.danger
                         : modelData.level === "warn" ? Theme.warning
                         : Theme.textSecondary
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WrapAnywhere
                }

                Text {
                    anchors.centerIn: parent
                    visible: logList.count === 0
                    text: qsTr("Nenhum log encontrado nos últimos 7 dias.")
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMeta
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: backend.systemLogsTruncated
            text: qsTr("Resultado truncado pelos limites de 10.000 linhas/5 MB.")
            color: Theme.warning
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.Wrap
        }

        Text {
            Layout.fillWidth: true
            visible: backend.systemLogsError.length > 0
            text: backend.systemLogsError
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.Wrap
        }

        Text {
            Layout.fillWidth: true
            visible: backend.diagnosticReportPath.length > 0
            text: qsTr("Relatório: %1").arg(backend.diagnosticReportPath)
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            elide: Text.ElideMiddle
        }
    }

    Timer {
        id: followTimer
        interval: 1500
        repeat: true
        running: followLogs.checked && dialog.visible
        onTriggered: backend.loadSystemLogs(dialog.levelFilter, dialog.componentFilter, 7)
    }

    Connections {
        target: backend
        function onSystemLogsChanged() {
            if (followLogs.checked) {
                logList.positionViewAtEnd()
            }
        }
    }

    FileDialog {
        id: exportDialog
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Logs JSON Lines (*.jsonl)")]
        onAccepted: backend.exportSystemLogs(selectedFile)
    }

    AppDialog {
        id: clearDialog
        title: qsTr("Limpar logs?")
        modal: true
        width: 420
        height: 190
        anchors.centerIn: parent

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: qsTr("Os arquivos de log do usuário serão apagados. Esta ação não pode ser desfeita.")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton {
                    text: qsTr("Cancelar")
                    size: "sm"
                    onClicked: clearDialog.close()
                }
                ActionButton {
                    text: qsTr("Limpar")
                    size: "sm"
                    onClicked: {
                        backend.clearSystemLogs()
                        clearDialog.close()
                    }
                }
            }
        }
    }
}
