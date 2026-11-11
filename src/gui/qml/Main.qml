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

    property string section: "containers"
    property string searchText: ""

    onSectionChanged: {
        searchText = ""
        searchField.text = ""
        refreshCurrent()
    }

    function applyAppearance() {
        const theme = backend.settingsValues["app.theme"] || "system"
        if (theme === "light") {
            Theme.dark = false
        } else if (theme === "dark") {
            Theme.dark = true
        } else {
            Theme.dark = Qt.styleHints.colorScheme !== Qt.ColorScheme.Light
        }
        const scale = Number(backend.settingsValues["app.fontScale"] || 100)
        Theme.fontScale = (isFinite(scale) && scale >= 50 && scale <= 200) ? scale / 100 : 1.0
    }

    function reveal() {
        appWindow.reveal()
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
        }

    }

    Connections {
        target: Qt.styleHints
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

        // Array JS em vez de ListModel: os roles defaultValue/currentValue
        // misturam string, número e bool, e o ListModel fixa o tipo no
        // primeiro append (String -> Bool falhava ao trocar de categoria).
        property var settingsRows: []

        readonly property var categories: [
            qsTr("Geral"), qsTr("Engine"), qsTr("Docker"), qsTr("Rede"),
            qsTr("Máquinas"), qsTr("Kubernetes"), qsTr("Terminal/Logs"),
            qsTr("Updates"), qsTr("Avançado"), qsTr("Setup")
        ]

        function rebuildSettingsRows() {
            const rows = []
            const schema = backend.settingsSchema
            const category = categoryBox.currentText
            for (let index = 0; index < schema.length; ++index) {
                const definition = schema[index]
                if (definition.category !== category) {
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
                    onCurrentTextChanged: settingsDialog.rebuildSettingsRows()
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
                        text: qsTr("Carregando schema de configurações…")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        horizontalAlignment: Text.AlignHCenter
                        topPadding: 20
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
