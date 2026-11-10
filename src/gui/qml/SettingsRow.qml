import QtQuick
import QtQuick.Layouts
import PodStack

// Linha de configuração gerada a partir do schema (T7-01).
Rectangle {
    id: root

    property string settingKey: ""
    property string settingType: "string"
    property var settingDefault: null
    property var enumValues: []
    property real minValue: 0
    property real maxValue: 0
    property bool restartRequired: false
    property bool secret: false
    property bool nullable: false
    property string description: ""
    property var currentValue: null
    property var settingsBackend: null

    property string error: ""

    signal saveRequested(string key, var value)
    signal resetRequested(string key)

    implicitHeight: layout.implicitHeight + 20
    radius: Theme.radiusSm
    color: Theme.bgSurface2
    border.width: 1
    border.color: error.length > 0 ? Theme.danger : Theme.border

    function formatValue(value) {
        if (value === null || value === undefined) {
            return "null"
        }
        if (typeof value === "boolean") {
            return value ? "true" : "false"
        }
        if (typeof value === "string") {
            return value
        }
        if (typeof value.length === "number") {
            return asArray(value).join(", ")
        }
        return String(value)
    }

    function asArray(value) {
        if (value === null || value === undefined) {
            return []
        }
        if (Array.isArray(value)) {
            return value
        }
        if (typeof value.length === "number") {
            const entries = []
            for (let index = 0; index < value.length; ++index) {
                entries.push(value[index])
            }
            return entries
        }
        return []
    }

    function parsedValue() {
        if (settingType === "bool") {
            return boolField.checked
        }
        if (settingType === "enum") {
            const choices = root.asArray(root.enumValues)
            return enumBox.currentIndex >= 0 ? choices[enumBox.currentIndex] : ""
        }
        if (settingType === "int") {
            return parseInt(textField.text, 10)
        }
        if (settingType === "int64") {
            return Number(textField.text)
        }
        if (settingType === "stringList") {
            const parts = textField.text.split(",")
            const entries = []
            for (let index = 0; index < parts.length; ++index) {
                const entry = parts[index].trim()
                if (entry.length > 0) {
                    entries.push(entry)
                }
            }
            return entries
        }
        if (settingType === "timestamp") {
            const text = textField.text.trim()
            return text.length === 0 || text === "null" ? null : text
        }
        const text = textField.text
        if (nullable && (text.trim().length === 0 || text.trim() === "null")) {
            return null
        }
        return text
    }

    ColumnLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        spacing: 7

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                RowLayout {
                    spacing: 6
                    Text {
                        text: root.settingKey
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMeta
                        font.weight: Font.DemiBold
                    }
                    AppBadge {
                        visible: root.restartRequired
                        text: qsTr("reinício")
                        tone: "warning"
                    }
                    AppBadge {
                        visible: root.secret
                        text: qsTr("segredo")
                        tone: "accent"
                    }
                }

                Text {
                    text: root.description
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Text {
                    text: qsTr("padrão: %1%2").arg(root.formatValue(root.settingDefault))
                          .arg(root.asArray(root.enumValues).length > 0
                               ? " · " + root.asArray(root.enumValues).join(" | ") : "")
                    color: Theme.textDisabled
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            FormCheck {
                id: boolField
                visible: root.settingType === "bool"
                text: qsTr("Ativado")
                checked: root.currentValue === true
                Layout.fillWidth: true
            }

            AppSelect {
                id: enumBox
                visible: root.settingType === "enum"
                model: root.enumValues
                currentIndex: Math.max(0, root.asArray(root.enumValues).indexOf(root.currentValue))
                Layout.fillWidth: true
            }

            FormField {
                id: textField
                visible: root.settingType !== "bool" && root.settingType !== "enum"
                Layout.fillWidth: true
                echoMode: root.secret ? TextInput.Password : TextInput.Normal
                placeholderText: root.settingType === "stringList"
                                 ? qsTr("itens separados por vírgula")
                                 : root.formatValue(root.settingDefault)
                text: {
                    if (root.settingType === "stringList") {
                        return root.asArray(root.currentValue).join(", ")
                    }
                    if (root.currentValue === null || root.currentValue === undefined) {
                        return ""
                    }
                    return String(root.currentValue)
                }
                validator: root.settingType === "int" ? intValidator
                           : root.settingType === "int64" ? doubleValidator : null
            }

            IntValidator {
                id: intValidator
                bottom: Math.floor(root.minValue)
                top: Math.ceil(root.maxValue)
            }

            DoubleValidator {
                id: doubleValidator
                bottom: root.minValue
                top: root.maxValue
                decimals: 0
                notation: DoubleValidator.StandardNotation
            }

            ActionButton {
                text: qsTr("Restaurar")
                size: "sm"
                onClicked: root.resetRequested(root.settingKey)
            }

            ActionButton {
                text: qsTr("Salvar")
                size: "sm"
                variant: "primary"
                onClicked: {
                    root.error = ""
                    root.saveRequested(root.settingKey, root.parsedValue())
                }
            }
        }

        Text {
            visible: root.error.length > 0
            text: root.error
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    Connections {
        target: root.settingsBackend
        function onSettingFinished(key, ok, message) {
            if (key !== root.settingKey) {
                return
            }
            root.error = ok ? "" : message
        }
    }
}
