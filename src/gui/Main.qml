import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design
import PodStack.Pages
import PodStack.Features

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
        const theme = backend.settings.values["app.theme"] || "system"
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
        const scale = Number(backend.settings.values["app.fontScale"] || 100)
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
        backend.settings.refresh()
        backend.setup.refreshStatus()
    }

    Connections {
        target: backend.setup

        function onChanged() {
            if (backend.setup.error.length > 0 && !backend.setup.completed) {
                appWindow.reveal()
            }
        }
    }

    Connections {
        target: backend.settings

        function onSchemaChanged() {
            if (settingsDialog.opened) {
                settingsDialog.rebuildSettingsRows()
            }
        }

        function onValuesChanged() {
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
            if ((backend.settings.values["app.theme"] || "system") === "system") {
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
            { key: "containers", label: qsTr("Containers"), icon: "containers" },
            { key: "images", label: qsTr("Images"), icon: "images" },
            { key: "volumes", label: qsTr("Volumes"), icon: "volumes" },
            { key: "networks", label: qsTr("Networks"), icon: "networks" },
            { key: "compose", label: qsTr("Compose"), icon: "compose" },
            { key: "builds", label: qsTr("Builds"), icon: "builds" },
            { key: "pods", label: qsTr("Pods"), icon: "pods" }
        ] }
        var linux = { group: "Linux", items: [
            { key: "machines", label: qsTr("Machines"), icon: "machines" },
            { key: "devices", label: qsTr("Devices"), icon: "devices", disabled: true }
        ] }
        return [engine, { group: "Kubernetes", items: [
            { key: "kubernetes", label: qsTr("Kubernetes"), icon: "pods" }
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
        return backend.settings.values["engine.mode"] === "rootful" ? "rootful" : "rootless"
    }

    // Ação do banner de erro: só oferece reiniciar o serviço quando o
    // podstackd está inacessível (não para erros de operação).
    readonly property string serviceActionText: {
        if (backend.connected) {
            return ""
        }
        return backend.serviceActivating ? qsTr("Reiniciando…") : qsTr("Reiniciar daemon")
    }

    function countFor(key) {
        if (key === "containers") {
            return containersPage.containerCount
        }
        if (key === "images") {
            return backend.images.items.length
        }
        if (key === "volumes") {
            return backend.volumes.items.length
        }
        if (key === "networks") {
            return backend.networks.items.length
        }
        if (key === "compose") {
            return backend.compose.items.length
        }
        if (key === "builds") {
            return backend.builds.items.length
        }
        if (key === "pods") {
            return backend.pods.items.length
        }
        if (key === "machines") {
            return backend.machines.items.length
        }
        if (key === "kubernetes") {
            return backend.k8s.pods.length + backend.k8s.services.length
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
            backend.images.refresh()
        } else if (section === "volumes") {
            backend.volumes.refresh()
        } else if (section === "networks") {
            backend.networks.refresh()
        } else if (section === "pods") {
            backend.pods.refresh()
        } else if (section === "compose") {
            backend.compose.refresh()
        } else if (section === "builds") {
            backend.builds.refresh()
        } else if (section === "machines") {
            backend.machines.refresh()
        } else if (section === "kubernetes") {
            backend.k8s.refresh()
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

                                    AppNavItem {
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

                AppNavItem {
                    Layout.fillWidth: true
                    iconName: "settings"
                    label: qsTr("Settings")
                    onClicked: settingsDialog.open()
                }

                AppNavItem {
                    Layout.fillWidth: true
                    iconName: "file"
                    label: qsTr("Logs")
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
                    AppSearchField {
                        id: searchField
                        visible: window.section === "containers"
                        Layout.preferredWidth: window.compactLayout ? 200 : 270
                        Layout.preferredHeight: 34
                        placeholderText: qsTr("Buscar containers…")
                        accessibleName: qsTr("Buscar containers")
                        onTextChanged: window.searchText = text
                    }

                    AppButton {
                        variant: "ghost"
                        iconName: Theme.dark ? "sun" : "moon"
                        onClicked: {
                            // Ciclo system → dark → light: antes o primeiro
                            // clique gravava dark/light e o app deixava de
                            // seguir o tema do desktop para sempre.
                            const current = backend.settings.values["app.theme"] || "system"
                            const next = current === "system" ? "dark"
                                       : current === "dark" ? "light" : "system"
                            backend.settings.setSetting("app.theme", next)
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
            AppErrorBanner {
                Layout.fillWidth: true
                message: backend.error
                actionText: window.serviceActionText
                onActionClicked: backend.restartDaemonService()
            }

            // Aviso de versão: GUI e daemon divergentes fazem a listagem
            // voltar vazia sem erro; avisar evita a confusão.
            AppErrorBanner {
                Layout.fillWidth: true
                tone: "warning"
                message: backend.daemonMismatch
                actionText: qsTr("Reiniciar daemon")
                onActionClicked: backend.restartDaemonService()
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

                    ImagesPage {
                        id: imagesPage
                        anchors.fill: parent
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    VolumesPage {
                        id: volumesPage
                        anchors.fill: parent
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    NetworksPage {
                        id: networksPage
                        anchors.fill: parent
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    ComposePage {
                        id: composePage
                        anchors.fill: parent
                        compact: window.compactLayout
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    BuildsPage {
                        id: buildsPage
                        anchors.fill: parent
                        compact: window.compactLayout
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    PodsPage {
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
                        text: backend.images.items.length + qsTr(" imagens")
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
                        text: backend.volumes.items.length + qsTr(" volumes")
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
                        text: backend.machines.items.length + qsTr(" machines")
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

    AppToast {
        message: backend.feedback.message
        severity: backend.feedback.severity
    }

    Connections {
        target: backend
        function onEventsChanged() {
            if (backend.eventMessage.length === 0) {
                return
            }
            const kind = backend.eventKind
            const severity = (kind === "die" || kind === "stop" || kind === "remove")
                             ? "warning"
                             : (kind === "start" || kind === "create") ? "success" : "info"
            backend.feedback.show(backend.eventMessage, severity, 3000)
        }
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
                backend.containers.lifecycle(item.state === "running" ? "stop" : "start", item.id)
            }
        }
    }

    AppModal {
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
                          .arg((backend.updates.status.latestVersion || ""))
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
                    text: backend.updates.status.notes || qsTr("Sem notas de release.")
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
                AppButton {
                    text: qsTr("Cancelar")
                    onClicked: updateInstallDialog.close()
                }
                AppButton {
                    text: qsTr("Instalar")
                    variant: "primary"
                    onClicked: {
                        updateInstallDialog.close()
                        backend.updates.install()
                    }
                }
            }
        }
    }

    SystemLogsDialog {
        id: systemLogsDialog
    }

    SettingsDialog {
        id: settingsDialog
    }

    Connections {
        target: backend
        function onContainersChanged(items) {
            containersPage.setContainers(items)
        }
        function onContainerCreated(id) {
            // Spec §3.1: depois de criar, a GUI seleciona o container novo.
            window.searchText = ""
            containersPage.filter = "all"
            containersPage.openContainer(id)
        }
    }

    Connections {
        target: tray
        function onOpenRequested() {
            window.reveal()
        }
        function onActivityMonitorRequested() {
            window.reveal()
            if (backend.setup.completed) {
                activityWindow.open()
            }
        }
        function onSettingsRequested() {
            window.reveal()
            if (backend.setup.completed) {
                settingsDialog.open()
            }
        }
        function onOpenContainerRequested(id, tab) {
            if (!backend.setup.completed) {
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
            if (!backend.setup.completed) {
                window.reveal()
                return
            }
            window.section = "compose"
            window.reveal()
            composePage.openProject(project, service, logs)
        }
        function onOpenViewRequested(section) {
            window.reveal()
            if (backend.setup.completed) {
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
