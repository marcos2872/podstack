import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

ColumnLayout {
    id: logsView

    // Quando `lines` é definido, a tela usa essa fonte (ex.: detalhe expandido)
    // em vez de `backend.logLines`. `clearRequested` permite que o dono da
    // fonte externa decida como limpar.
    property var lines: null
    property string emptyText: qsTr("Sem logs. Assine os logs do container para acompanhar a saída.")

    signal clearRequested()

    spacing: 8

    readonly property var sourceLines: logsView.lines !== null ? logsView.lines
                                                              : backend.logLines

    readonly property var settingsValues: backend.settingsValues
    readonly property int logFontSize: {
        const size = Number(settingsValues["logs.fontSize"] || 12)
        return isFinite(size) && size >= 8 && size <= 32 ? size : 12
    }
    readonly property bool prettyJson: settingsValues["logs.prettyJson"] !== false

    FormField {
        id: search
        Layout.fillWidth: true
        Layout.preferredHeight: 30
        placeholderText: qsTr("Filtrar logs…")
        Accessible.name: qsTr("Filtrar logs")
        onTextChanged: logsView.rebuild()
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        FormCheck {
            id: follow
            text: qsTr("Seguir")
            checked: true
            onCheckedChanged: if (checked) logsView.scrollToEnd()
        }

        FormCheck {
            id: wrapLines
            text: qsTr("Wrap")
            checked: logsView.settingsValues["logs.wrapLines"] === true
            onCheckedChanged: logsView.rebuild()
        }

        FormCheck {
            id: timestamps
            text: qsTr("Timestamp")
            checked: true
            onCheckedChanged: logsView.rebuild()
        }

        Item { Layout.fillWidth: true }

        ActionButton {
            text: qsTr("Copiar")
            iconName: "copy"
            variant: "secondary"
            size: "sm"
            enabled: logArea.text.length > 0
            onClicked: {
                logArea.selectAll()
                logArea.copy()
                logArea.deselect()
                logArea.cursorPosition = logArea.length
            }
            Accessible.name: qsTr("Copiar logs")
        }

        ActionButton {
            text: qsTr("Limpar")
            variant: "secondary"
            size: "sm"
            onClicked: {
                if (logsView.lines !== null) {
                    logsView.clearRequested()
                } else {
                    backend.clearLogs()
                }
            }
            Accessible.name: qsTr("Limpar logs")
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

        ScrollView {
            id: logScroll
            anchors.fill: parent
            clip: true
            ScrollBar.horizontal.policy: wrapLines.checked ? ScrollBar.AlwaysOff
                                                           : ScrollBar.AsNeeded
            ScrollBar.vertical: ScrollBar {}

            TextArea {
                id: logArea
                readOnly: true
                selectByMouse: true
                persistentSelection: true
                textFormat: TextEdit.PlainText
                color: Theme.textSecondary
                selectionColor: Theme.indigo500
                selectedTextColor: Theme.textPrimary
                font.family: Theme.monoFamily
                font.pixelSize: logsView.logFontSize
                wrapMode: wrapLines.checked ? TextEdit.WrapAnywhere : TextEdit.NoWrap
                width: wrapLines.checked
                       ? logScroll.availableWidth
                       : Math.max(logScroll.availableWidth, implicitWidth)
                padding: 10
                background: null
                Accessible.name: qsTr("Saída dos logs")
            }
        }

        Text {
            anchors.centerIn: parent
            width: parent.width - 32
            visible: logArea.text.length === 0
            horizontalAlignment: Text.AlignHCenter
            text: logsView.emptyText
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMeta
            wrapMode: Text.WordWrap
        }
    }

    function formattedLine(line) {
        let text = line
        if (!timestamps.checked) {
            const space = line.indexOf(" ")
            text = space > 0 ? line.substring(space + 1) : line
        }
        if (logsView.prettyJson) {
            const trimmed = text.trim()
            const looksJson = (trimmed.startsWith("{") && trimmed.endsWith("}"))
                              || (trimmed.startsWith("[") && trimmed.endsWith("]"))
            if (looksJson) {
                try {
                    return JSON.stringify(JSON.parse(trimmed), null, 2)
                } catch (error) {
                    return text
                }
            }
        }
        return text
    }

    function rebuild() {
        const source = logsView.sourceLines || []
        const flick = logScroll.contentItem
        const previousY = flick ? flick.contentY : 0
        const wasAtEnd = !flick
                          || previousY >= Math.max(0, logScroll.contentHeight - flick.height) - 4
        const selectedStart = logArea.selectionStart
        const selectedEnd = logArea.selectionEnd
        const hadSelection = selectedEnd > selectedStart
        const needle = search.text
        const filtered = []

        for (let i = 0; i < source.length; ++i) {
            const line = String(source[i])
            if (needle === "" || line.indexOf(needle) >= 0) {
                filtered.push(formattedLine(line))
            }
        }

        const nextText = filtered.join("\n")
        if (logArea.text !== nextText) {
            logArea.text = nextText
            if (hadSelection) {
                const start = Math.min(selectedStart, nextText.length)
                const end = Math.min(selectedEnd, nextText.length)
                logArea.select(start, end)
            } else {
                logArea.cursorPosition = logArea.length
            }
        }

        if (hadSelection) {
            return
        }
        if (follow.checked && wasAtEnd) {
            Qt.callLater(logsView.scrollToEnd)
        } else if (flick) {
            Qt.callLater(function() {
                flick.contentY = Math.min(previousY,
                                           Math.max(0, logScroll.contentHeight - flick.height))
            })
        }
    }

    function scrollToEnd() {
        const flick = logScroll.contentItem
        if (flick) {
            flick.contentY = Math.max(0, logScroll.contentHeight - flick.height)
        }
    }

    onLinesChanged: Qt.callLater(logsView.rebuild)

    Connections {
        target: backend
        function onLogsChanged() {
            if (logsView.lines === null) {
                logsView.rebuild()
            }
        }
    }

    Component.onCompleted: rebuild()
}
