import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack.Design

Item {
    id: containersPage

    property var allItems: []
    property var visibleItems: []
    property var selectedIds: ({})
    property int anchorIndex: -1
    property string currentId: ""
    property string pendingOpenId: ""
    property string filterText: ""
    property string filter: "all"

    signal expandRequested(string id)

    readonly property int containerCount: allItems.length
    readonly property int runningCount: {
        let count = 0
        for (let i = 0; i < allItems.length; ++i) {
            if (allItems[i].state === "running") {
                ++count
            }
        }
        return count
    }
    readonly property int stoppedCount: containerCount - runningCount
    readonly property int selectedCount: Object.keys(selectedIds).length
    property bool compact: false

    readonly property var filterTabs: [
        { key: "all", label: qsTr("Todos"), count: containerCount },
        { key: "running", label: qsTr("Em execução"), count: runningCount },
        { key: "stopped", label: qsTr("Parados"), count: stoppedCount }
    ]

    onFilterTextChanged: rebuild()
    onFilterChanged: rebuild()

    function shortId(item) {
        if (!item || !item.id) {
            return ""
        }
        return String(item.id).substring(0, 12)
    }

    // FR-CON-001: idade do container a partir do `created` do engine.
    function ageText(item) {
        if (!item || !item.created) {
            return ""
        }
        const created = Date.parse(item.created)
        if (isNaN(created)) {
            return ""
        }
        const minutes = Math.max(0, Math.floor((Date.now() - created) / 60000))
        if (minutes < 1) {
            return qsTr("agora")
        }
        if (minutes < 60) {
            return qsTr("%1 min").arg(minutes)
        }
        const hours = Math.floor(minutes / 60)
        if (hours < 24) {
            return qsTr("%1 h").arg(hours)
        }
        return qsTr("%1 d").arg(Math.floor(hours / 24))
    }

    function portsText(item) {
        if (!item || !item.ports) {
            return ""
        }
        if (Array.isArray(item.ports)) {
            return item.ports.join(", ")
        }
        return String(item.ports)
    }

    function idAt(index) {
        if (index < 0 || index >= visibleItems.length) {
            return ""
        }
        return visibleItems[index].id
    }

    function currentItem() {
        for (let i = 0; i < visibleItems.length; ++i) {
            if (visibleItems[i].id === currentId) {
                return visibleItems[i]
            }
        }
        return ({})
    }

    function rebuild() {
        const needle = filterText.toLowerCase()
        const next = []
        for (let i = 0; i < allItems.length; ++i) {
            const item = allItems[i]
            const running = item.state === "running"
            if (filter === "running" && !running) {
                continue
            }
            if (filter === "stopped" && running) {
                continue
            }
            if (needle.length > 0) {
                const haystack = (item.name + " " + item.image + " " + item.id + " "
                                  + (item.project || "") + " " + (item.service || "") + " "
                                  + portsText(item) + " " + (item.status || "")).toLowerCase()
                if (haystack.indexOf(needle) < 0) {
                    continue
                }
            }
            next.push(item)
        }

        visibleItems = next

        let index = -1
        for (let i = 0; i < next.length; ++i) {
            if (next[i].id === currentId) {
                index = i
                break
            }
        }
        if (index < 0 && next.length > 0) {
            index = 0
            for (let i = 0; i < next.length; ++i) {
                if (next[i].state === "running") {
                    index = i
                    break
                }
            }
        }
        containerList.currentIndex = index
        currentId = index >= 0 ? next[index].id : ""
        if (index >= 0 && !selectedIds[currentId]) {
            selectOnly(index)
        }
        pruneSelection()
        applyPendingOpen()
    }

    function openContainer(id) {
        pendingOpenId = id
        applyPendingOpen()
    }

    function applyPendingOpen() {
        if (pendingOpenId.length === 0) {
            return
        }
        for (let i = 0; i < visibleItems.length; ++i) {
            if (visibleItems[i].id === pendingOpenId) {
                currentId = pendingOpenId
                containerList.currentIndex = i
                selectOnly(i)
                pendingOpenId = ""
                return
            }
        }
    }

    function pruneSelection() {
        const next = {}
        let changed = false
        for (const id in selectedIds) {
            let found = false
            for (let i = 0; i < allItems.length; ++i) {
                if (allItems[i].id === id) {
                    found = true
                    break
                }
            }
            if (found) {
                next[id] = true
            } else {
                changed = true
            }
        }
        if (changed) {
            selectedIds = next
        }
    }

    function selectOnly(index) {
        const id = idAt(index)
        if (id === "") {
            return
        }
        const next = {}
        next[id] = true
        selectedIds = next
        anchorIndex = index
        currentId = id
    }

    function toggleSelection(index) {
        const id = idAt(index)
        if (id === "") {
            return
        }
        const next = Object.assign({}, selectedIds)
        if (next[id]) {
            delete next[id]
        } else {
            next[id] = true
        }
        selectedIds = next
        anchorIndex = index
        currentId = id
    }

    function selectRange(index) {
        if (anchorIndex < 0) {
            selectOnly(index)
            return
        }
        const from = Math.min(anchorIndex, index)
        const to = Math.max(anchorIndex, index)
        const next = {}
        for (let row = from; row <= to; ++row) {
            const id = idAt(row)
            if (id !== "") {
                next[id] = true
            }
        }
        selectedIds = next
    }

    function clearSelection() {
        selectedIds = {}
        anchorIndex = -1
    }

    function setContainers(items) {
        allItems = items
        rebuild()
    }

    function openCreate() {
        createImage.text = ""
        createName.text = ""
        createCommand.text = ""
        createPorts.text = ""
        createEnv.text = ""
        createMounts.text = ""
        createLabels.text = ""
        createWorkDir.text = ""
        createUser.text = ""
        createCpus.text = ""
        createMemory.text = ""
        createNetwork.currentIndex = 0
        createRestart.currentIndex = 0
        createRemove.checked = false
        createInit.checked = false
        createPrivileged.checked = false
        createReadOnly.checked = false
        createExposeLan.checked = false
        pendingSpec = ({})
        createError = ""
        createMissingImage = ""
        createDialog.open()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: containersPage.compact ? 14 : 22
        anchors.rightMargin: containersPage.compact ? 20 : 22
        anchors.topMargin: 20
        anchors.bottomMargin: 20
        spacing: 0

        // Cabeçalho da view
        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 16
            spacing: 12

            ColumnLayout {
                spacing: 4

                Text {
                    text: qsTr("Containers")
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontDisplay
                    font.weight: Font.DemiBold
                    font.letterSpacing: -0.7
                }

                Text {
                    text: containersPage.containerCount + qsTr(" containers · ")
                          + "<font color=\"" + Theme.success + "\">"
                          + containersPage.runningCount + qsTr(" em execução</font> · ")
                          + containersPage.stoppedCount + qsTr(" parados")
                    textFormat: Text.StyledText
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }
            }

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("Novo container")
                variant: "primary"
                iconName: "plus"
                onClicked: containersPage.openCreate()
            }

            AppButton {
                text: containersPage.compact ? "" : qsTr("Start all")
                iconName: "play"
                onClicked: backend.containers.batchAll("start")
                Accessible.name: qsTr("Start all")
            }

            AppButton {
                text: containersPage.compact ? "" : qsTr("Stop all")
                iconName: "stop"
                onClicked: backend.containers.batchAll("stop")
                Accessible.name: qsTr("Stop all")
            }

            AppButton {
                variant: "ghost"
                iconName: "refresh"
                onClicked: backend.refresh()
                Accessible.name: qsTr("Atualizar")
            }
        }

        // Filtros
        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 16
            spacing: 10

            Rectangle {
                implicitWidth: tabsRow.implicitWidth + 6
                implicitHeight: 32
                radius: Theme.radiusSm
                color: Theme.bgSurface2
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    id: tabsRow
                    anchors.fill: parent
                    anchors.margins: 3
                    spacing: 2

                    Repeater {
                        model: containersPage.filterTabs

                        ItemDelegate {
                            id: filterTab
                            readonly property bool active: containersPage.filter === modelData.key

                            implicitHeight: 26
                            implicitWidth: tabLabel.implicitWidth + 20
                            hoverEnabled: true
                            Accessible.name: modelData.label

                            background: Rectangle {
                                radius: Theme.radiusXs
                                color: filterTab.active ? Theme.bgElevated
                                                        : filterTab.hovered ? Theme.bgHover
                                                                            : "transparent"
                            }

                            contentItem: Text {
                                id: tabLabel
                                text: modelData.label + "  " + modelData.count
                                color: filterTab.active ? Theme.textPrimary : Theme.textTertiary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMeta
                                font.weight: Font.Medium
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            onClicked: containersPage.filter = modelData.key
                        }
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Text {
                text: containersPage.visibleItems.length + qsTr(" de ")
                      + containersPage.containerCount
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
            }
        }

        // Lista + inspector
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: containersPage.compact ? 12 : 16

            Rectangle {
                id: listPanel
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: containersPage.compact ? 278 : 380
                color: Theme.bgSurface
                radius: Theme.radiusLg
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    // Cabeçalho do painel
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 44
                        Layout.leftMargin: 14
                        Layout.rightMargin: 14
                        spacing: 10

                        Text {
                            text: qsTr("Containers")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            font.weight: Font.DemiBold
                        }

                        Text {
                            text: qsTr("ordem do engine")
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMeta
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            visible: containersPage.selectedCount > 0 && listPanel.width > 560
                            text: containersPage.selectedCount + qsTr(" selecionados")
                            color: Theme.indigo400
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMeta
                        }

                        AppButton {
                            visible: containersPage.selectedCount > 0
                            text: listPanel.width > 560 ? qsTr("Iniciar") : ""
                            iconName: "play"
                            size: "sm"
                            Accessible.name: qsTr("Iniciar")
                            onClicked: backend.containers.batchIds(Object.keys(containersPage.selectedIds), "start")
                        }

                        AppButton {
                            visible: containersPage.selectedCount > 0
                            text: listPanel.width > 560 ? qsTr("Parar") : ""
                            iconName: "stop"
                            size: "sm"
                            Accessible.name: qsTr("Parar")
                            onClicked: backend.containers.batchIds(Object.keys(containersPage.selectedIds), "stop")
                        }

                        AppButton {
                            visible: containersPage.selectedCount > 0
                            text: listPanel.width > 560 ? qsTr("Remover") : ""
                            variant: "danger"
                            iconName: "trash"
                            size: "sm"
                            Accessible.name: qsTr("Remover")
                            onClicked: batchRemoveDialog.open()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.border
                    }

                    ListView {
                        id: containerList
                        objectName: "containerList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        activeFocusOnTab: true
                        Accessible.role: Accessible.List
                        Accessible.name: qsTr("Containers")
                        model: containersPage.visibleItems
                        ScrollBar.vertical: ScrollBar {}

                        Keys.onUpPressed: {
                            if (currentIndex > 0) {
                                currentIndex -= 1
                                containersPage.selectOnly(currentIndex)
                            }
                        }
                        Keys.onDownPressed: {
                            if (currentIndex + 1 < containersPage.visibleItems.length) {
                                currentIndex += 1
                                containersPage.selectOnly(currentIndex)
                            }
                        }
                        Keys.onReturnPressed: {
                            if (containersPage.currentId.length > 0) {
                                containersPage.openContainer(containersPage.currentId)
                            }
                        }
                        Keys.onEscapePressed: containersPage.clearSelection()

                        delegate: Item {
                            id: containerRow

                            width: ListView.view.width
                            height: Theme.rowHeight
                            Accessible.name: modelData.name
                            Accessible.role: Accessible.ListItem
                            Accessible.selected: containersPage.selectedIds[modelData.id] === true

                            readonly property bool hovered: rowHover.hovered

                            HoverHandler { id: rowHover }

                            Rectangle {
                                anchors.fill: parent
                                color: containersPage.selectedIds[modelData.id]
                                       ? Theme.accentSoft
                                       : containerRow.hovered
                                         ? Qt.rgba(Theme.textPrimary.r, Theme.textPrimary.g,
                                                   Theme.textPrimary.b, 0.03)
                                         : "transparent"

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 1
                                    color: Theme.border
                                    visible: index < containersPage.visibleItems.length - 1
                                }
                            }

                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                onClicked: function(mouse) {
                                    containerList.forceActiveFocus()
                                    containerList.currentIndex = index
                                    containersPage.currentId = modelData.id
                                    if (mouse.modifiers & Qt.ControlModifier) {
                                        containersPage.toggleSelection(index)
                                    } else if (mouse.modifiers & Qt.ShiftModifier) {
                                        containersPage.selectRange(index)
                                    } else {
                                        containersPage.selectOnly(index)
                                    }
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 14
                                anchors.rightMargin: 14
                                spacing: 12

                                Rectangle {
                                    Layout.preferredWidth: 8
                                    Layout.preferredHeight: 8
                                    radius: 4
                                    color: modelData.state === "running"
                                           ? (modelData.health === "unhealthy" ? Theme.danger
                                              : modelData.health === "starting" ? Theme.warning
                                                                               : Theme.success)
                                           : Theme.textTertiary
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        Text {
                                            text: modelData.name
                                            color: Theme.textPrimary
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontUi
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }

                                        AppBadge {
                                            visible: (modelData.project || "").length > 0
                                            text: modelData.project
                                            tone: "accent"
                                        }

                                        AppBadge {
                                            visible: modelData.health === "healthy"
                                            text: qsTr("healthy")
                                            tone: "success"
                                        }

                                        AppBadge {
                                            visible: modelData.health === "unhealthy"
                                            text: qsTr("unhealthy")
                                            tone: "danger"
                                        }

                                        AppBadge {
                                            visible: modelData.health === "starting"
                                            text: qsTr("starting")
                                            tone: "warning"
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.image
                                              + (containersPage.shortId(modelData).length > 0
                                                 ? "  ·  " + containersPage.shortId(modelData) : "")
                                              + (containersPage.ageText(modelData).length > 0
                                                 ? "  ·  " + containersPage.ageText(modelData) : "")
                                              + (containersPage.portsText(modelData).length > 0
                                                 ? "  ·  " + containersPage.portsText(modelData) : "")
                                        color: Theme.textTertiary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontMeta
                                        elide: Text.ElideRight
                                    }
                                }

                                RowLayout {
                                    spacing: 7

                                    Text {
                                        text: (modelData.status || "").length > 0
                                              ? modelData.status : modelData.state
                                        color: modelData.state === "running"
                                               ? (modelData.health === "unhealthy" ? Theme.danger
                                                  : modelData.health === "starting" ? Theme.warning
                                                                                    : Theme.success)
                                               : Theme.textTertiary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontMeta
                                    }

                                    RowLayout {
                                        spacing: 4
                                        opacity: containerRow.hovered || containersPage.selectedIds[modelData.id]
                                                 ? 1 : 0
                                        visible: opacity > 0

                                        Behavior on opacity {
                                            NumberAnimation { duration: Theme.duration }
                                        }

                                        AppButton {
                                            size: "sm"
                                            variant: "ghost"
                                            iconName: modelData.state === "running" ? "stop" : "play"
                                            onClicked: backend.containers.lifecycle(
                                                           modelData.state === "running" ? "stop" : "start",
                                                           modelData.id)
                                            Accessible.name: modelData.state === "running"
                                                             ? qsTr("Parar") : qsTr("Iniciar")
                                        }

                                        AppButton {
                                            size: "sm"
                                            variant: "ghost"
                                            iconName: "rotate"
                                            onClicked: backend.containers.lifecycle("restart", modelData.id)
                                            Accessible.name: qsTr("Reiniciar")
                                        }
                                    }
                                }
                            }
                        }

                        // Estado vazio / carregando / erro
                        ColumnLayout {
                            anchors.centerIn: parent
                            width: parent.width - 48
                            visible: containerList.count === 0
                            spacing: 10

                            // Carregando: não confundir com engine vazio.
                            AppSpinner {
                                Layout.alignment: Qt.AlignHCenter
                                visible: backend.containers.loading && backend.connected
                            }

                            AppIcon {
                                Layout.alignment: Qt.AlignHCenter
                                visible: !backend.containers.loading || !backend.connected
                                name: backend.connected ? "containers" : "alert"
                                size: 24
                                color: Theme.textTertiary
                            }

                            Text {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                visible: backend.containers.loading && backend.connected
                                text: qsTr("Carregando containers…")
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                visible: !backend.containers.loading && backend.connected
                                text: qsTr("Nenhum container encontrado.\nCrie um pelo botão “Novo container”.")
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                visible: !backend.connected
                                text: backend.error.length > 0
                                      ? backend.error
                                      : qsTr("O podstackd não está acessível.")
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                wrapMode: Text.WordWrap
                            }

                            RowLayout {
                                Layout.alignment: Qt.AlignHCenter
                                visible: !backend.connected
                                spacing: 8

                                AppButton {
                                    text: qsTr("Tentar novamente")
                                    size: "sm"
                                    variant: "secondary"
                                    enabled: !backend.containers.loading
                                    onClicked: backend.refresh()
                                }

                                AppButton {
                                    text: backend.serviceActivating
                                          ? qsTr("Reiniciando…")
                                          : qsTr("Reiniciar daemon")
                                    size: "sm"
                                    variant: "primary"
                                    enabled: !backend.serviceActivating
                                    onClicked: backend.restartDaemonService()
                                }
                            }
                        }
                    }
                }
            }

            AppInspector {
                Layout.preferredWidth: containersPage.compact ? 320 : 480
                Layout.minimumWidth: containersPage.compact ? 280 : 360
                Layout.fillHeight: true
                selected: containerList.currentIndex >= 0
                          && containerList.currentIndex < containersPage.visibleItems.length
                          ? containersPage.visibleItems[containerList.currentIndex]
                          : null
                onExpandRequested: function(id) {
                    containersPage.expandRequested(id)
                }
            }
        }
    }

    // Criação: mantém o diálogo aberto enquanto o daemon responde, mostra o
    // erro no próprio formulário e oferece o pull quando falta a imagem.
    property var pendingSpec: ({})
    property string createError: ""
    property string createMissingImage: ""
    readonly property bool creating: backend.containers.actionActive

    readonly property var networkOptions: {
        var options = [qsTr("Padrão")]
        var items = backend.networks.items || []
        for (var i = 0; i < items.length; ++i) {
            var name = items[i].name || ""
            if (name.length > 0 && name !== "podman") {
                options.push(name)
            }
        }
        return options
    }

    readonly property var restartOptions: [
        { label: qsTr("Padrão do engine"), value: "" },
        { label: qsTr("Nunca (no)"), value: "no" },
        { label: qsTr("Sempre (always)"), value: "always" },
        { label: qsTr("Se não parado manualmente (unless-stopped)"), value: "unless-stopped" },
        { label: qsTr("Em caso de falha (on-failure)"), value: "on-failure" }
    ]
    readonly property var restartLabels: {
        var labels = []
        for (var i = 0; i < restartOptions.length; ++i) {
            labels.push(restartOptions[i].label)
        }
        return labels
    }

    Connections {
        target: backend.containers
        function onActionFailed(method, code, message) {
            if (method !== "containers.create" || !createDialog.opened) {
                return
            }
            createError = message
            if (code === "PODSTACK_IMAGE_NOT_FOUND") {
                createMissingImage = createImage.text.trim()
            }
        }
        function onContainerCreated(id) {
            if (createDialog.opened) {
                createDialog.close()
            }
        }
    }

    Connections {
        target: backend.images
        function onPullCompleted(ok, reference, error) {
            if (!createDialog.opened || createMissingImage.length === 0
                    || reference !== createMissingImage) {
                return
            }
            if (!ok) {
                createError = error
                return
            }
            createMissingImage = ""
            createError = ""
            backend.containers.createContainer(pendingSpec, true)
        }
    }

    AppModal {
        id: batchRemoveDialog
        title: qsTr("Remover containers?")
        modal: true
        width: 440
        anchors.centerIn: parent
        actionController: backend.containers
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.border
        }
        contentItem: Text {
            Layout.fillWidth: true
            text: qsTr("%1 container(s) serão removidos permanentemente.").arg(containersPage.selectedCount)
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            wrapMode: Text.WordWrap
        }
        footer: AppFooter {
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                enabled: !batchRemoveDialog.busy
                onClicked: batchRemoveDialog.close()
            }
            AppButton {
                text: qsTr("Remover")
                variant: "danger"
                busy: batchRemoveDialog.busy
                busyText: qsTr("Removendo…")
                enabled: !batchRemoveDialog.busy
                onClicked: batchRemoveDialog.runAction(function() {
                    backend.containers.batchIds(Object.keys(containersPage.selectedIds), "remove")
                }, "containers.batch")
            }
        }
    }

    AppModal {
        id: createDialog
        title: qsTr("Novo container")
        modal: true
        width: 720
        anchors.centerIn: parent
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.border
        }
        onOpened: {
            createError = ""
            createMissingImage = ""
        }
        contentItem: ColumnLayout {
            spacing: 10

            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(createForm.implicitHeight, 430)
                clip: true
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    id: createForm
                    width: parent.width - 16
                    spacing: 8

                    GridLayout {
                        columns: 2
                        Layout.fillWidth: true
                        columnSpacing: 10
                        rowSpacing: 6

                        Text { text: qsTr("Imagem *"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                        AppInput {
                            id: createImage
                            Layout.fillWidth: true
                            placeholderText: "nginx:alpine"
                        }

                        Text { text: qsTr("Nome"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                        AppInput {
                            id: createName
                            Layout.fillWidth: true
                            placeholderText: qsTr("opcional")
                        }

                        Text { text: qsTr("Comando"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                        AppInput {
                            id: createCommand
                            Layout.fillWidth: true
                            placeholderText: qsTr("opcional, sem shell (use sh -c \"...\")")
                        }

                        Text { text: qsTr("Portas"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                        AppInput {
                            id: createPorts
                            Layout.fillWidth: true
                            placeholderText: "8080:80, 127.0.0.1:8081:81"
                        }

                        Text { text: qsTr("Env"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                        AppInput {
                            id: createEnv
                            Layout.fillWidth: true
                            placeholderText: "A=B, C=D"
                        }

                        Text { text: qsTr("Volumes"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                        AppInput {
                            id: createMounts
                            Layout.fillWidth: true
                            placeholderText: "/host:/container, /tmp:/mnt:ro"
                        }

                        Text { text: qsTr("Labels"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                        AppInput {
                            id: createLabels
                            Layout.fillWidth: true
                            placeholderText: "chave=valor, outra=valor"
                        }

                        Text { text: qsTr("Workdir"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                        AppInput {
                            id: createWorkDir
                            Layout.fillWidth: true
                            placeholderText: qsTr("opcional")
                        }

                        Text { text: qsTr("Usuário"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                        AppInput {
                            id: createUser
                            Layout.fillWidth: true
                            placeholderText: qsTr("opcional (uid:gid)")
                        }

                        Text { text: qsTr("Rede"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                        AppSelect {
                            id: createNetwork
                            Layout.fillWidth: true
                            model: containersPage.networkOptions
                            currentIndex: 0
                        }

                        Text { text: qsTr("Restart"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                        AppSelect {
                            id: createRestart
                            Layout.fillWidth: true
                            model: containersPage.restartLabels
                            currentIndex: 0
                        }

                        Text { text: qsTr("CPUs"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                        AppInput {
                            id: createCpus
                            Layout.fillWidth: true
                            placeholderText: qsTr("opcional (1.5 = 1,5 CPU)")
                            validator: DoubleValidator { bottom: 0; top: 1024; decimals: 3 }
                        }

                        Text { text: qsTr("Memória (MB)"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                        AppInput {
                            id: createMemory
                            Layout.fillWidth: true
                            placeholderText: qsTr("opcional")
                            validator: IntValidator { bottom: 0; top: 1048576 }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        AppCheck { id: createRemove; text: qsTr("Remover ao parar") }
                        AppCheck { id: createInit; text: qsTr("Init") }
                        AppCheck { id: createPrivileged; text: qsTr("Privileged") }
                        AppCheck { id: createReadOnly; text: qsTr("Read-only") }
                        Item { Layout.fillWidth: true }
                    }

                    AppCheck {
                        id: createExposeLan
                        text: qsTr("Expor portas na LAN")
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: createExposeLan.checked
                        text: qsTr("Abre as portas publicadas no firewall (firewalld/ufw) e permite acesso de outros dispositivos da rede. Só portas publicadas são afetadas. O acesso é revertido com \u201cFechar LAN\u201d no inspector.")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // Erro do create fica no formulário; o diálogo não fecha e os
            // campos digitados continuam lá.
            Text {
                Layout.fillWidth: true
                visible: createError.length > 0
                text: createError
                color: Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                visible: createMissingImage.length > 0
                spacing: 8

                AppButton {
                    text: backend.images.pullActive ? qsTr("Baixando…") : qsTr("Baixar imagem")
                    variant: "secondary"
                    size: "sm"
                    enabled: !backend.images.pullActive
                    onClicked: backend.images.pullImage(createMissingImage)
                }
                AppSpinner {
                    visible: backend.images.pullActive
                }
                Text {
                    Layout.fillWidth: true
                    text: backend.images.pullStatus
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMicro
                    elide: Text.ElideRight
                }
            }
        }
        footer: AppFooter {
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                onClicked: {
                    if (backend.images.pullActive) {
                        backend.images.cancelPull()
                    }
                    createDialog.close()
                }
            }
            AppButton {
                text: containersPage.creating ? qsTr("Criando…") : qsTr("Criar e iniciar")
                variant: "primary"
                enabled: createImage.text.trim().length > 0 && !containersPage.creating
                onClicked: {
                    createError = ""
                    createMissingImage = ""
                    var restartIndex = createRestart.currentIndex
                    var networkIndex = createNetwork.currentIndex
                    containersPage.pendingSpec = {
                        image: createImage.text,
                        name: createName.text,
                        command: createCommand.text,
                        portsText: createPorts.text,
                        envText: createEnv.text,
                        mountsText: createMounts.text,
                        labelsText: createLabels.text,
                        workDir: createWorkDir.text,
                        user: createUser.text,
                        network: networkIndex > 0 ? containersPage.networkOptions[networkIndex] : "",
                        restartPolicy: restartIndex >= 0 && restartIndex < containersPage.restartOptions.length
                                        ? containersPage.restartOptions[restartIndex].value : "",
                        cpus: parseFloat(createCpus.text) || 0,
                        memoryMb: parseInt(createMemory.text, 10) || 0,
                        remove: createRemove.checked,
                        init: createInit.checked,
                        privileged: createPrivileged.checked,
                        readOnly: createReadOnly.checked,
                        exposeLan: createExposeLan.checked
                    }
                    backend.containers.createContainer(containersPage.pendingSpec, true)
                }
            }
        }
    }
}
