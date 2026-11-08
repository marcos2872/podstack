import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: logsView

    Layout.margins: 14
    spacing: 8

    FormField {
        id: search
        Layout.fillWidth: true
        Layout.preferredHeight: 30
        placeholderText: qsTr("Filtrar logs…")
        onTextChanged: logsView.rebuild()
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        FormCheck {
            id: follow
            text: qsTr("Seguir")
            checked: true
        }

        FormCheck {
            id: wrapLines
            text: qsTr("Wrap")
            checked: true
        }

        FormCheck {
            id: timestamps
            text: qsTr("Timestamp")
            checked: true
        }

        Item { Layout.fillWidth: true }

        ActionButton {
            text: qsTr("Limpar")
            variant: "secondary"
            size: "sm"
            onClicked: backend.clearLogs()
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        color: Theme.terminalBg
        radius: Theme.radiusSm
        border.width: 1
        border.color: Theme.border
        clip: true

        ListView {
            id: logsList
            anchors.fill: parent
            clip: true
            model: ListModel { id: logModel }
            ScrollBar.vertical: ScrollBar {}

            delegate: Text {
                width: logsList.width
                text: {
                    const line = model.line
                    if (timestamps.checked) {
                        return line
                    }
                    const space = line.indexOf(" ")
                    return space > 0 ? line.substring(space + 1) : line
                }
                color: Theme.textSecondary
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontMeta
                textFormat: Text.PlainText
                wrapMode: wrapLines.checked ? Text.WrapAnywhere : Text.NoWrap
                padding: 10
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 32
                visible: logsList.count === 0
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Sem logs. Assine os logs do container para acompanhar a saída.")
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
                wrapMode: Text.WordWrap
            }
        }
    }

    function rebuild() {
        logModel.clear()
        const lines = backend.logLines
        const needle = search.text
        for (let i = 0; i < lines.length; ++i) {
            const line = lines[i]
            if (needle === "" || line.indexOf(needle) >= 0) {
                logModel.append({ line: line })
            }
        }
        if (follow.checked) {
            logsList.positionViewAtEnd()
        }
    }

    Connections {
        target: backend
        function onLogsChanged() {
            logsView.rebuild()
        }
    }

    Component.onCompleted: rebuild()
}
