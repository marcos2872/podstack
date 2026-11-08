import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: logsView

    Theme { id: theme }

    spacing: 6

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        FormField {
            id: search
            Layout.fillWidth: true
            placeholderText: "Filtrar logs..."
            onTextChanged: logsView.rebuild()
        }

        FormCheck {
            id: follow
            text: "Seguir"
            checked: true
        }

        FormCheck {
            id: wrapLines
            text: "Wrap"
            checked: true
        }

        FormCheck {
            id: timestamps
            text: "Timestamp"
            checked: true
        }

        ActionButton {
            text: "Limpar"
            accentColor: theme.bgElevated
            onClicked: backend.clearLogs()
        }
    }

    ListView {
        id: logsList
        Layout.fillWidth: true
        Layout.fillHeight: true
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
            color: theme.textSecondary
            font.family: "monospace"
            font.pixelSize: 10
            textFormat: Text.PlainText
            wrapMode: wrapLines.checked ? Text.WrapAnywhere : Text.NoWrap
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
