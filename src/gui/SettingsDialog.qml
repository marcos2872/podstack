import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

// Diálogo de configurações (schema dinâmico do daemon).
AppModal {
    id: settingsDialog
    objectName: "settingsDialog"
    title: qsTr("Configurações")
    modal: true
    width: Math.min(880, parent.width - 48)
    height: Math.min(680, parent.height - 48)
    anchors.centerIn: parent
    leftPadding: 18
    rightPadding: 18
    topPadding: 12
    bottomPadding: 0
    onOpened: {
        backend.settings.refresh()
        rebuildSettingsRows()
    }
    onAutoCheckUpdatesChanged: autoCheckBox.checked = autoCheckUpdates

    // Array JS em vez de ListModel: os roles defaultValue/currentValue
    // misturam string, número e bool, e o ListModel fixa o tipo no
    // primeiro append (String -> Bool falhava ao trocar de categoria).
    property var settingsRows: []

    // Atualizações são internas do app: o diálogo expõe só o interruptor
    // (updates.mode) e a verificação manual na aba Geral. O restante
    // (canal, feed, chave) fica oculto no schema e disponível na CLI.
    property bool autoCheckUpdates: true

    function updateStatusText() {
        const status = backend.updates.status || {}
        if (status.status === "ok") {
            if (status.updateAvailable) {
                return qsTr("Versão %1 disponível.").arg(status.latestVersion || "")
            }
            return qsTr("Você está na versão mais recente.")
        }
        if (status.status === "disabled") {
            return qsTr("Verificação automática desativada.")
        }
        if (status.status === "unconfigured") {
            return qsTr("Canal de atualização não configurado.")
        }
        if (status.status === "no-feed") {
            return qsTr("Feed de atualizações indisponível.")
        }
        if (status.status === "installed") {
            return qsTr("Atualização instalada; reabra o app se necessário.")
        }
        if (status.status === "failed") {
            return qsTr("Atualização falhou: %1").arg(status.reason || "")
        }
        if (status.reason) {
            return status.reason
        }
        return qsTr("Ainda não verificado.")
    }

    // `value` é a categoria do schema (pt-BR, estável no daemon); `label`
    // é só o texto traduzido exibido no select.
    readonly property var categories: [
        { value: "Geral", label: qsTr("Geral") },
        { value: "Engine", label: qsTr("Engine") },
        { value: "Docker", label: qsTr("Docker") },
        { value: "Rede", label: qsTr("Rede") },
        { value: "Máquinas", label: qsTr("Máquinas") },
        { value: "Kubernetes", label: qsTr("Kubernetes") },
        { value: "Terminal/Logs", label: qsTr("Terminal/Logs") },
        { value: "Notificações", label: qsTr("Notificações") },
        { value: "Avançado", label: qsTr("Avançado") },
        { value: "Setup", label: qsTr("Setup") }
    ]

    function rebuildSettingsRows() {
        const rows = []
        const schema = backend.settings.schema
        const category = categoryBox.currentValue
        const updateMode = backend.settings.values["updates.mode"] || "check"
        settingsDialog.autoCheckUpdates = updateMode !== "off"
        for (let index = 0; index < schema.length; ++index) {
            const definition = schema[index]
            // `hidden` = chave ainda sem efeito no app; o schema/CLI continua
            // expondo, mas o diálogo não promete o que não funciona.
            if (definition.category !== category || definition.hidden === true) {
                continue
            }
            const value = backend.settings.values[definition.key]
            rows.push({
                settingKey: definition.key,
                settingType: definition.type,
                defaultValue: definition["default"],
                enumValues: definition.enum || [],
                minValue: definition.min !== undefined ? definition.min : 0,
                maxValue: definition.max !== undefined ? definition.max : 0,
                restartRequired: definition.restartRequired === true,
                secret: definition.secret === true,
                nullable: definition.nullable === true,
                description: definition.description || "",
                currentValue: value === undefined ? null : value
            })
        }
        settingsDialog.settingsRows = rows
    }

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                text: qsTr("Categoria")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
                font.weight: Font.DemiBold
            }

            AppSelect {
                id: categoryBox
                Layout.preferredWidth: 220
                model: settingsDialog.categories
                textRole: "label"
                valueRole: "value"
                onCurrentValueChanged: settingsDialog.rebuildSettingsRows()
            }

            Item { Layout.fillWidth: true }

            Text {
                text: qsTr("%1 chaves").arg(settingsDialog.settingsRows.length)
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        Rectangle {
            Layout.fillWidth: true
            visible: backend.settings.pendingRestartKeys.length > 0
            implicitHeight: restartRow.implicitHeight + 12
            radius: Theme.radiusSm
            color: Theme.warningSoft
            border.width: 1
            border.color: Theme.warning

            RowLayout {
                id: restartRow
                anchors.fill: parent
                anchors.margins: 6
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Feche e abra o PodStack para aplicar: %1")
                              .arg(backend.settings.pendingRestartKeys.join(", "))
                    color: Theme.warning
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                    verticalAlignment: Text.AlignVCenter
                }

                AppButton {
                    text: qsTr("Fechar agora")
                    size: "sm"
                    variant: "secondary"
                    onClicked: appWindow.quitApp()
                }
            }
        }

        ScrollView {
            id: settingsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            Column {
                id: settingsColumn
                width: settingsScroll.availableWidth
                spacing: Theme.spacingSm

                Rectangle {
                    id: updateCheckRow
                    objectName: "updateCheckRow"
                    visible: categoryBox.currentValue === "Geral"
                    width: settingsColumn.width
                    implicitHeight: updateCheckLayout.implicitHeight + 20
                    radius: Theme.radiusSm
                    color: Theme.bgSurface2
                    border.width: 1
                    border.color: Theme.border

                    ColumnLayout {
                        id: updateCheckLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 10
                        spacing: 7

                        Text {
                            text: qsTr("Atualizações do PodStack")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMeta
                            font.weight: Font.DemiBold
                        }

                        AppCheck {
                            id: autoCheckBox
                            objectName: "updateAutoCheckBox"
                            text: qsTr("Verificar atualizações ao iniciar")
                            checked: settingsDialog.autoCheckUpdates
                            Layout.fillWidth: true
                            onClicked: backend.settings.setSetting("updates.mode",
                                                          checked ? "check" : "off")
                        }

                        Text {
                            text: qsTr("A verificação consulta o manifesto assinado do canal estável e nunca instala nada sozinha.")
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMicro
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            AppButton {
                                objectName: "updateInstallButton"
                                text: qsTr("Instalar atualização")
                                size: "sm"
                                visible: (backend.updates.status.updateAvailable || false)
                                         === true
                                onClicked: updateInstallDialog.open()
                            }

                            AppButton {
                                objectName: "updateCheckNowButton"
                                text: qsTr("Verificar atualizações")
                                size: "sm"
                                onClicked: backend.updates.check(true)
                            }

                            Text {
                                Layout.fillWidth: true
                                text: settingsDialog.updateStatusText()
                                color: Theme.textTertiary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMicro
                                wrapMode: Text.WordWrap
                            }

                            AppButton {
                                objectName: "updateViewLogButton"
                                text: qsTr("Ver log")
                                size: "sm"
                                visible: backend.updates.status.status === "failed"
                                onClicked: systemLogsDialog.openFor("update", "error")
                            }

                            AppButton {
                                objectName: "updateResumeButton"
                                text: qsTr("Retomar")
                                size: "sm"
                                visible: backend.updates.status.phase === "installing"
                                         || backend.updates.status.phase === "staged"
                                onClicked: backend.updates.resume()
                            }

                            AppButton {
                                objectName: "updateClearStateButton"
                                text: qsTr("Limpar estado")
                                size: "sm"
                                visible: backend.updates.status.status === "failed"
                                         || backend.updates.status.phase === "installing"
                                         || backend.updates.status.phase === "staged"
                                onClicked: backend.updates.clearState()
                            }
                        }
                    }
                }

                Repeater {
                    model: settingsDialog.settingsRows

                    delegate: AppSettingsRow {
                        width: settingsColumn.width
                        settingKey: modelData.settingKey
                        settingType: modelData.settingType
                        settingDefault: modelData.defaultValue
                        enumValues: modelData.enumValues
                        minValue: modelData.minValue
                        maxValue: modelData.maxValue
                        restartRequired: modelData.restartRequired
                        secret: modelData.secret
                        nullable: modelData.nullable
                        description: modelData.description
                        currentValue: modelData.currentValue
                        settingsBackend: backend

                        onSaveRequested: function(key, value) {
                            backend.settings.setSetting(key, value)
                        }
                        onResetRequested: function(key) {
                            backend.settings.resetSetting(key)
                        }
                    }
                }

                Text {
                    visible: settingsDialog.settingsRows.length === 0
                    width: parent.width
                    text: backend.settings.schema.length === 0
                          ? qsTr("Carregando schema de configurações…")
                          : qsTr("Nenhuma configuração nesta categoria.")
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    horizontalAlignment: Text.AlignHCenter
                    topPadding: 20
                    wrapMode: Text.WordWrap
                }
            }
        }

        Text {
            visible: backend.settings.error.length > 0
            text: backend.settings.error
            color: Theme.danger
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        AppButton {
            visible: backend.settings.error.length > 0
            text: qsTr("Ver log")
            size: "sm"
            onClicked: systemLogsDialog.openFor("setup", "")
        }
    }

    footer: AppFooter {
        AppButton {
            text: qsTr("Executar configuração")
            iconName: "settings"
            onClicked: {
                settingsDialog.close()
                setupWindow.openSetup(true)
            }
        }
        Item { Layout.fillWidth: true }
        AppButton {
            text: qsTr("Fechar")
            variant: "primary"
            onClicked: settingsDialog.close()
        }
    }
}
