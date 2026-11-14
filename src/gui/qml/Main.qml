import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

// Conteúdo da janela principal. O QMainWindow (MainWindow) define tamanho,
// visibilidade e o painel de terminal; este item apenas apresenta a UI.
Rectangle {
    id: window

    color: Theme.bgBase

    readonly property bool compactLayout: width < 1100
    readonly property bool compactStatus: width < 860

    FontLoader { source: "qrc:/qt/qml/PodStack/assets/fonts/Inter-Regular.ttf" }
    FontLoader { source: "qrc:/qt/qml/PodStack/assets/fonts/Inter-Medium.ttf" }
    FontLoader { source: "qrc:/qt/qml/PodStack/assets/fonts/Inter-SemiBold.ttf" }
    FontLoader { source: "qrc:/qt/qml/PodStack/assets/fonts/Inter-Bold.ttf" }

    property string section: startupSection
    property string searchText: ""
    property bool sidebarVisible: true

    onSectionChanged: {
        searchText = ""
        searchField.text = ""
        refreshCurrent()
        appWindow.rememberSection(section)
    }

    function applyAppearance() {
        const theme = backend.settingsValues["app.theme"] || "system"
        if (theme === "light") {
            Theme.dark = false
        } else if (theme === "dark") {
            Theme.dark = true
        } else {
            const hints = Qt.styleHints
            const scheme = (hints && hints.colorScheme !== undefined) ? hints.colorScheme : undefined
            if (typeof Qt.ColorScheme !== "undefined" && scheme !== undefined) {
                Theme.dark = scheme !== Qt.ColorScheme.Light
            } else {
                // Qt 6.4 não expõe Qt.ColorScheme; mantém o tema claro.
                Theme.dark = false
            }
        }
        const scale = Number(backend.settingsValues["app.fontScale"] || 100)
        Theme.fontScale = (isFinite(scale) && scale >= 80 && scale <= 150) ? scale / 100 : 1.0
    }

    function reveal() {
        appWindow.reveal()
    }

    function openSection(section) {
        window.section = section
        reveal()
    }

    Component.onCompleted: {
        applyAppearance()
        backend.refreshSettings()
        backend.setupRefresh()
    }

    Connections {
        target: backend

        function onSetupChanged() {
            if (backend.setupError.length > 0 && !backend.setupCompleted) {
                appWindow.reveal()
            }
        }

        function onSettingsValuesChanged() {
            applyAppearance()
            // Salvar/restaurar atualiza os valores no daemon; sem reconstruir as
            // linhas o campo continuaria exibindo o snapshot antigo.
            if (settingsDialog.opened) {
                settingsDialog.rebuildSettingsRows()
            }
        }

    }

    Connections {
        target: Qt.styleHints
        ignoreUnknownSignals: true
        function onColorSchemeChanged() {
            if ((backend.settingsValues["app.theme"] || "system") === "system") {
                applyAppearance()
            }
        }
    }

    SetupWindow {
        id: setupWindow
        anchors.fill: parent
        z: 100
    }

    ActivityMonitorWindow {
        id: activityWindow
    }

    readonly property var sections: {
        var engine = { group: "Engine", items: [
            { key: "containers", label: "Containers", icon: "containers" },
            { key: "images", label: "Images", icon: "images" },
            { key: "volumes", label: "Volumes", icon: "volumes" },
            { key: "networks", label: "Networks", icon: "networks" },
            { key: "compose", label: "Compose", icon: "compose" },
            { key: "builds", label: "Builds", icon: "builds" },
            { key: "pods", label: "Pods", icon: "pods" }
        ] }
        var linux = { group: "Linux", items: [
            { key: "machines", label: "Machines", icon: "machines" },
            { key: "devices", label: "Devices", icon: "devices", disabled: true }
        ] }
        return [engine, { group: "Kubernetes", items: [
            { key: "kubernetes", label: "Kubernetes", icon: "pods" }
        ] }, linux]
    }

    readonly property string sectionTitle: {
        for (let group = 0; group < sections.length; ++group) {
            const items = sections[group].items
            for (let item = 0; item < items.length; ++item) {
                if (items[item].key === section) {
                    return items[item].label
                }
            }
        }
        return "Containers"
    }

    readonly property string engineMode: {
        return backend.settingsValues["engine.mode"] === "rootful" ? "rootful" : "rootless"
    }

    function countFor(key) {
        if (key === "containers") {
            return containersPage.containerCount
        }
        if (key === "images") {
            return backend.images.length
        }
        if (key === "volumes") {
            return backend.volumes.length
        }
        if (key === "networks") {
            return backend.networks.length
        }
        if (key === "compose") {
            return backend.composeProjects.length
        }
        if (key === "builds") {
            return backend.builds.length
        }
        if (key === "pods") {
            return backend.pods.length
        }
        if (key === "machines") {
            return backend.machines.length
        }
        if (key === "kubernetes") {
            return backend.k8sPods.length + backend.k8sServices.length
        }
        return -1
    }

    function viewIndexFor(key) {
        if (key === "images") {
            return 1
        }
        if (key === "volumes") {
            return 2
        }
        if (key === "networks") {
            return 3
        }
        if (key === "compose") {
            return 4
        }
        if (key === "builds") {
            return 5
        }
        if (key === "pods") {
            return 6
        }
        if (key === "machines") {
            return 7
        }
        if (key === "kubernetes") {
            return 8
        }
        return 0
    }

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
        } else if (section === "compose") {
            backend.refreshCompose()
        } else if (section === "builds") {
            backend.refreshBuilds()
        } else if (section === "machines") {
            backend.refreshMachines()
        } else if (section === "kubernetes") {
            backend.refreshK8s()
        }
    }

    RowLayout {
        id: mainContent
        anchors.fill: parent
        enabled: !setupWindow.visible
        spacing: 0

        // Sidebar
        Rectangle {
            id: sidebar
            visible: window.sidebarVisible
            Layout.preferredWidth: window.compactLayout ? 196 : Theme.sidebarWidth
            Layout.fillHeight: true
            color: Theme.bgSidebar

            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: Theme.border
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.topMargin: 14
                anchors.bottomMargin: 12
                spacing: 2

                // Marca
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.bottomMargin: 14
                    spacing: 10

                    Image {
                        source: "qrc:/qt/qml/PodStack/assets/podstack-icon.svg"
                        sourceSize: Qt.size(80, 80)
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        Layout.alignment: Qt.AlignVCenter
                        fillMode: Image.PreserveAspectFit
                    }

                    ColumnLayout {
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 0
                        Text {
                            text: "PodStack"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontTitle
                            font.weight: Font.Bold
                            font.letterSpacing: -0.3
                        }
                        Text {
                            text: "v" + backend.version
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMeta
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                Flickable {
                    id: navScroll

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: width
                    contentHeight: navColumn.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    ColumnLayout {
                        id: navColumn

                        width: navScroll.width
                        spacing: 2

                        Repeater {
                            model: window.sections

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    Layout.leftMargin: 10
                                    Layout.topMargin: 10
                                    Layout.bottomMargin: 6
                                    text: modelData.group
                                    color: Theme.textTertiary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontMeta
                                    font.weight: Font.DemiBold
                                    font.capitalization: Font.AllUppercase
                                    font.letterSpacing: 0.9
                                }

                                Repeater {
                                    model: modelData.items

                                    NavItem {
                                        Layout.fillWidth: true
                                        iconName: modelData.icon
                                        label: modelData.label
                                        count: window.countFor(modelData.key)
                                        current: window.section === modelData.key
                                        available: modelData.disabled !== true
                                        onClicked: window.section = modelData.key
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    Layout.leftMargin: 4
                    Layout.rightMargin: 4
                    Layout.bottomMargin: 4
                    color: Theme.border
                }

                NavItem {
                    Layout.fillWidth: true
                    iconName: "settings"
                    label: "Settings"
                    onClicked: settingsDialog.open()
                }

                NavItem {
                    Layout.fillWidth: true
                    iconName: "file"
                    label: "Logs"
                    onClicked: systemLogsDialog.open()
                }
            }
        }

        // Workspace
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Topbar
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.topbarHeight
                color: Theme.bgBase

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.border
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    spacing: 12

                    Text {
                        text: "PodStack"
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                    }
                    Text {
                        text: "/"
                        color: Theme.textDisabled
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                    }
                    Text {
                        text: window.sectionTitle
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                    }

                    Item { Layout.fillWidth: true }

                    // Busca
                    Rectangle {
                        visible: window.section === "containers"
                        Layout.preferredWidth: window.compactLayout ? 200 : 270
                        Layout.preferredHeight: 34
                        radius: Theme.radiusSm
                        color: Theme.bgSurface2
                        border.width: 1
                        border.color: searchField.activeFocus ? Theme.indigo500 : Theme.border

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 7

                            AppIcon {
                                name: "search"
                                size: 14
                                color: Theme.textTertiary
                            }

                            TextField {
                                id: searchField
                                Layout.fillWidth: true
                                Accessible.name: qsTr("Buscar containers")
                                placeholderText: qsTr("Buscar containers…")
                                color: Theme.textPrimary
                                placeholderTextColor: Theme.textTertiary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                leftPadding: 0
                                rightPadding: 0
                                verticalAlignment: TextInput.AlignVCenter
                                background: null
                                selectByMouse: true
                                selectionColor: Theme.accentSoft
                                selectedTextColor: Theme.textPrimary
                                onTextChanged: window.searchText = text
                            }
                        }
                    }

                    ActionButton {
                        variant: "ghost"
                        iconName: Theme.dark ? "sun" : "moon"
                        onClicked: {
                            Theme.dark = !Theme.dark
                            backend.saveTheme(Theme.dark ? "dark" : "light")
                        }
                        Accessible.name: qsTr("Alternar tema")
                    }

                    // Engine pill
                    Rectangle {
                        implicitWidth: engineRow.implicitWidth + 20
                        implicitHeight: 28
                        radius: 999
                        color: Theme.bgSurface2
                        border.width: 1
                        border.color: Theme.border

                        RowLayout {
                            id: engineRow
                            anchors.centerIn: parent
                            spacing: 7

                            Rectangle {
                                Layout.preferredWidth: 6
                                Layout.preferredHeight: 6
                                radius: 3
                                color: backend.connected ? Theme.success : Theme.danger
                            }

                            Text {
                                text: window.engineMode
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMeta
                                font.weight: Font.Medium
                            }
                        }
                    }
                }
            }

            // Error banner
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: backend.error.length > 0 ? 34 : 0
                visible: backend.error.length > 0
                color: Theme.dangerSoft

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 20
                    spacing: 8

                    AppIcon {
                        name: "alert"
                        size: 14
                        color: Theme.danger
                    }

                    Text {
                        Layout.fillWidth: true
                        text: backend.error
                        color: Theme.danger
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                    }
                }
            }

            // Páginas
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: window.viewIndexFor(window.section)

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    ContainersPage {
                        id: containersPage
                        anchors.fill: parent
                        filterText: window.searchText
                        compact: window.compactLayout
                        onExpandRequested: function(id) {
                            appWindow.openContainerDetail(id)
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    ImagesView {
                        id: imagesPage
                        anchors.fill: parent
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    VolumesView {
                        id: volumesPage
                        anchors.fill: parent
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    NetworksView {
                        id: networksPage
                        anchors.fill: parent
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    ComposeView {
                        id: composePage
                        anchors.fill: parent
                        compact: window.compactLayout
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    BuildsView {
                        id: buildsPage
                        anchors.fill: parent
                        compact: window.compactLayout
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    PodsView {
                        id: podsPage
                        anchors.fill: parent
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    MachinesPage {
                        id: machinesPage
                        anchors.fill: parent
                        onMachineExpandRequested: function(id, tab) {
                            appWindow.openMachineDetail(id, tab)
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    KubernetesPage {
                        id: kubernetesPage
                        objectName: "kubernetesPage"
                        anchors.fill: parent
                    }
                }
            }

            // Statusbar
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.statusbarHeight
                color: Theme.bgSidebar

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 1
                    color: Theme.border
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 20
                    spacing: 8

                    Rectangle {
                        Layout.preferredWidth: 6
                        Layout.preferredHeight: 6
                        radius: 3
                        color: backend.connected ? Theme.success : Theme.danger
                    }

                    Text {
                        text: backend.connected ? qsTr("podstackd conectado")
                                                : qsTr("podstackd desconectado")
                        color: backend.connected ? Theme.success : Theme.danger
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: containersPage.runningCount + "/" + containersPage.containerCount
                              + qsTr(" containers")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }

                    Text {
                        visible: !window.compactStatus
                        text: "·"
                        color: Theme.textDisabled
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }

                    Text {
                        visible: !window.compactStatus
                        text: backend.images.length + qsTr(" imagens")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }

                    Text {
                        visible: !window.compactStatus
                        text: "·"
                        color: Theme.textDisabled
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }

                    Text {
                        visible: !window.compactStatus
                        text: backend.volumes.length + qsTr(" volumes")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }

                    Text {
                        visible: !window.compactStatus
                        text: "·"
                        color: Theme.textDisabled
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }

                    Text {
                        visible: !window.compactStatus
                        text: backend.machines.length + qsTr(" machines")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }

                    Text {
                        visible: !window.compactStatus
                        text: "·"
                        color: Theme.textDisabled
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }

                    Text {
                        text: "GPL-3.0-or-later"
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                    }
                }
            }
        }
    }

    Toast {
        message: backend.eventMessage
        kind: backend.eventKind
    }

    Shortcut {
        sequence: StandardKey.Find
        enabled: !setupWindow.visible && window.section === "containers"
        onActivated: searchField.forceActiveFocus()
    }

    Shortcut {
        objectName: "shortcutNew"
        sequence: StandardKey.New
        enabled: !setupWindow.visible && window.section === "containers"
        onActivated: containersPage.openCreate()
    }

    Shortcut {
        objectName: "shortcutSettings"
        sequence: StandardKey.Preferences
        enabled: !setupWindow.visible
        onActivated: settingsDialog.open()
    }

    Shortcut {
        objectName: "shortcutSidebar"
        sequences: [ "Ctrl+B" ]
        enabled: !setupWindow.visible
        onActivated: window.sidebarVisible = !window.sidebarVisible
    }

    Shortcut {
        objectName: "shortcutRefresh"
        sequences: [ StandardKey.Refresh, "F5" ]
        enabled: !setupWindow.visible
        onActivated: window.refreshCurrent()
    }

    Shortcut {
        objectName: "shortcutQuit"
        sequence: StandardKey.Quit
        onActivated: appWindow.quitApp()
    }

    Shortcut {
        objectName: "shortcutContainerLogs"
        sequences: [ "Ctrl+Shift+L" ]
        enabled: !setupWindow.visible && window.section === "containers"
                 && containersPage.currentId.length > 0
        onActivated: appWindow.openContainerDetailTab(containersPage.currentId, 1)
    }

    Shortcut {
        objectName: "shortcutContainerTerminal"
        sequences: [ "Ctrl+Shift+T" ]
        enabled: !setupWindow.visible && window.section === "containers"
                 && containersPage.currentId.length > 0
        onActivated: appWindow.openContainerDetailTab(containersPage.currentId, 7)
    }

    Shortcut {
        objectName: "shortcutContainerLifecycle"
        sequences: [ "Ctrl+Return", "Ctrl+Enter" ]
        enabled: !setupWindow.visible && window.section === "containers"
                 && containersPage.currentId.length > 0
        onActivated: {
            const item = containersPage.currentItem()
            if (item && item.id) {
                backend.lifecycle(item.state === "running" ? "stop" : "start", item.id)
            }
        }
    }

    AppDialog {
        id: updateInstallDialog
        objectName: "updateInstallDialog"
        title: qsTr("Instalar atualização")
        modal: true
        width: Math.min(620, window.width - 48)
        height: Math.min(420, window.height - 48)
        anchors.centerIn: parent

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: qsTr("PodStack %1 será baixado, verificado e instalado. A GUI será fechada e reaberta ao final.")
                          .arg((backend.updateStatus.latestVersion || ""))
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                wrapMode: Text.Wrap
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    readOnly: true
                    text: backend.updateStatus.notes || qsTr("Sem notas de release.")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMeta
                    wrapMode: Text.Wrap
                    background: null
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton {
                    text: qsTr("Cancelar")
                    onClicked: updateInstallDialog.close()
                }
                ActionButton {
                    text: qsTr("Instalar")
                    variant: "primary"
                    onClicked: {
                        updateInstallDialog.close()
                        backend.installUpdate()
                    }
                }
            }
        }
    }

    SystemLogsDialog {
        id: systemLogsDialog
    }

    AppDialog {
        id: settingsDialog
        objectName: "settingsDialog"
        title: qsTr("Configurações")
        modal: true
        width: Math.min(880, window.width - 48)
        height: Math.min(680, window.height - 48)
        anchors.centerIn: parent
        leftPadding: 18
        rightPadding: 18
        topPadding: 12
        bottomPadding: 0
        onOpened: {
            backend.refreshSettings()
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
            const status = backend.updateStatus || {}
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
            const schema = backend.settingsSchema
            const category = categoryBox.currentValue
            const updateMode = backend.settingsValues["updates.mode"] || "check"
            settingsDialog.autoCheckUpdates = updateMode !== "off"
            for (let index = 0; index < schema.length; ++index) {
                const definition = schema[index]
                // `hidden` = chave ainda sem efeito no app; o schema/CLI continua
                // expondo, mas o diálogo não promete o que não funciona.
                if (definition.category !== category || definition.hidden === true) {
                    continue
                }
                const value = backend.settingsValues[definition.key]
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
                visible: backend.pendingRestartKeys.length > 0
                implicitHeight: restartNotice.implicitHeight + 12
                radius: Theme.radiusSm
                color: Theme.warningSoft
                border.width: 1
                border.color: Theme.warning

                Text {
                    id: restartNotice
                    anchors.fill: parent
                    anchors.margins: 6
                    text: qsTr("Reinicie o PodStack para aplicar: %1")
                              .arg(backend.pendingRestartKeys.join(", "))
                    color: Theme.warning
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                    verticalAlignment: Text.AlignVCenter
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

                            FormCheck {
                                id: autoCheckBox
                                objectName: "updateAutoCheckBox"
                                text: qsTr("Verificar atualizações ao iniciar")
                                checked: settingsDialog.autoCheckUpdates
                                Layout.fillWidth: true
                                onClicked: backend.setSetting("updates.mode",
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

                                ActionButton {
                                    objectName: "updateInstallButton"
                                    text: qsTr("Instalar atualização")
                                    size: "sm"
                                    visible: (backend.updateStatus.updateAvailable || false)
                                             === true
                                    onClicked: updateInstallDialog.open()
                                }

                                ActionButton {
                                    objectName: "updateCheckNowButton"
                                    text: qsTr("Verificar atualizações")
                                    size: "sm"
                                    onClicked: backend.checkForUpdates(true)
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: settingsDialog.updateStatusText()
                                    color: Theme.textTertiary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontMicro
                                    wrapMode: Text.WordWrap
                                }

                                ActionButton {
                                    objectName: "updateViewLogButton"
                                    text: qsTr("Ver log")
                                    size: "sm"
                                    visible: backend.updateStatus.status === "failed"
                                    onClicked: systemLogsDialog.openFor("update", "error")
                                }

                                ActionButton {
                                    objectName: "updateResumeButton"
                                    text: qsTr("Retomar")
                                    size: "sm"
                                    visible: backend.updateStatus.phase === "installing"
                                             || backend.updateStatus.phase === "staged"
                                    onClicked: backend.resumeUpdate()
                                }

                                ActionButton {
                                    objectName: "updateClearStateButton"
                                    text: qsTr("Limpar estado")
                                    size: "sm"
                                    visible: backend.updateStatus.status === "failed"
                                             || backend.updateStatus.phase === "installing"
                                             || backend.updateStatus.phase === "staged"
                                    onClicked: backend.clearUpdateState()
                                }
                            }
                        }
                    }

                    Repeater {
                        model: settingsDialog.settingsRows

                        delegate: SettingsRow {
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
                                backend.setSetting(key, value)
                            }
                            onResetRequested: function(key) {
                                backend.resetSetting(key)
                            }
                        }
                    }

                    Text {
                        visible: settingsDialog.settingsRows.length === 0
                        width: parent.width
                        text: backend.settingsSchema.length === 0
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
                visible: backend.settingsError.length > 0
                text: backend.settingsError
                color: Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ActionButton {
                visible: backend.settingsError.length > 0
                text: qsTr("Ver log")
                size: "sm"
                onClicked: systemLogsDialog.openFor("setup", "")
            }
        }

        footer: AppFooter {
            ActionButton {
                text: qsTr("Executar configuração")
                iconName: "settings"
                onClicked: {
                    settingsDialog.close()
                    setupWindow.openSetup(true)
                }
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Fechar")
                variant: "primary"
                onClicked: settingsDialog.close()
            }
        }
    }

    Connections {
        target: backend
        function onContainersChanged(items) {
            containersPage.setContainers(items)
        }
        function onSettingsSchemaChanged() {
            settingsDialog.rebuildSettingsRows()
        }
        function onSettingsValuesChanged() {
            settingsDialog.rebuildSettingsRows()
        }
    }

    Connections {
        target: tray
        function onOpenRequested() {
            window.reveal()
        }
        function onActivityMonitorRequested() {
            window.reveal()
            if (backend.setupCompleted) {
                activityWindow.open()
            }
        }
        function onSettingsRequested() {
            window.reveal()
            if (backend.setupCompleted) {
                settingsDialog.open()
            }
        }
        function onOpenContainerRequested(id, tab) {
            if (!backend.setupCompleted) {
                window.reveal()
                return
            }
            window.searchText = ""
            containersPage.filter = "all"
            window.section = "containers"
            window.reveal()
            containersPage.openContainer(id)
            // O tray mantém o atalho de logs: o modal de detalhe abre na aba Logs.
            if (tab === "logs") {
                appWindow.openContainerDetailTab(id, 1)
            }
        }
        function onOpenComposeRequested(project, service, logs) {
            if (!backend.setupCompleted) {
                window.reveal()
                return
            }
            window.section = "compose"
            window.reveal()
            composePage.openProject(project, service, logs)
        }
        function onOpenViewRequested(section) {
            window.reveal()
            if (backend.setupCompleted) {
                window.section = section
            }
        }
    }

    Connections {
        target: notifications
        function onContainerActivated(id) {
            tray.requestOpenContainer(id, "logs")
        }
    }

    Connections {
        target: singleInstance
        function onShowRequested() {
            window.reveal()
        }
    }
}
