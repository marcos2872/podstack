import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

Item {
    id: containersPage

    property var allItems: []
    property var visibleItems: []
    property var selectedIds: ({})
    property int anchorIndex: -1
    property string currentId: ""
    property string filterText: ""
    property string filter: "all"

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
                const haystack = (item.name + " " + item.image + " " + portsText(item)
                                  + " " + (item.status || "")).toLowerCase()
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
        createWorkDir.text = ""
        createUser.text = ""
        createRemove.checked = false
        createInit.checked = false
        createPrivileged.checked = false
        createDialog.open()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: containersPage.compact ? 14 : 22
        anchors.rightMargin: containersPage.compact ? 14 : 22
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

            ActionButton {
                text: qsTr("Novo container")
                variant: "primary"
                iconName: "plus"
                onClicked: containersPage.openCreate()
            }

            ActionButton {
                text: containersPage.compact ? "" : qsTr("Start all")
                iconName: "play"
                onClicked: backend.batchAll("start")
                Accessible.name: qsTr("Start all")
            }

            ActionButton {
                text: containersPage.compact ? "" : qsTr("Stop all")
                iconName: "stop"
                onClicked: backend.batchAll("stop")
                Accessible.name: qsTr("Stop all")
            }

            ActionButton {
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
                Layout.minimumWidth: containersPage.compact ? 280 : 380
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

                        ActionButton {
                            visible: containersPage.selectedCount > 0
                            text: listPanel.width > 560 ? qsTr("Iniciar") : ""
                            iconName: "play"
                            size: "sm"
                            Accessible.name: qsTr("Iniciar")
                            onClicked: backend.batchIds(Object.keys(containersPage.selectedIds), "start")
                        }

                        ActionButton {
                            visible: containersPage.selectedCount > 0
                            text: listPanel.width > 560 ? qsTr("Parar") : ""
                            iconName: "stop"
                            size: "sm"
                            Accessible.name: qsTr("Parar")
                            onClicked: backend.batchIds(Object.keys(containersPage.selectedIds), "stop")
                        }

                        ActionButton {
                            visible: containersPage.selectedCount > 0
                            text: listPanel.width > 560 ? qsTr("Remover") : ""
                            variant: "danger"
                            iconName: "trash"
                            size: "sm"
                            Accessible.name: qsTr("Remover")
                            onClicked: backend.batchIds(Object.keys(containersPage.selectedIds), "remove")
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.border
                    }

                    ListView {
                        id: containerList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: containersPage.visibleItems
                        ScrollBar.vertical: ScrollBar {}

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

                                        ActionButton {
                                            size: "sm"
                                            variant: "ghost"
                                            iconName: modelData.state === "running" ? "stop" : "play"
                                            onClicked: backend.lifecycle(
                                                           modelData.state === "running" ? "stop" : "start",
                                                           modelData.id)
                                            Accessible.name: modelData.state === "running"
                                                             ? qsTr("Parar") : qsTr("Iniciar")
                                        }

                                        ActionButton {
                                            size: "sm"
                                            variant: "ghost"
                                            iconName: "rotate"
                                            onClicked: backend.lifecycle("restart", modelData.id)
                                            Accessible.name: qsTr("Reiniciar")
                                        }
                                    }
                                }
                            }
                        }

                        // Estado vazio / erro
                        ColumnLayout {
                            anchors.centerIn: parent
                            width: parent.width - 48
                            visible: containerList.count === 0
                            spacing: 6

                            AppIcon {
                                Layout.alignment: Qt.AlignHCenter
                                name: backend.connected ? "containers" : "alert"
                                size: 24
                                color: Theme.textTertiary
                            }

                            Text {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                text: backend.connected
                                      ? qsTr("Nenhum container encontrado.\nCrie um container pela CLI: podstack run ...")
                                      : qsTr("podstackd não está acessível.\nVerifique: systemctl --user status podstack.socket")
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }

            Inspector {
                Layout.preferredWidth: containersPage.compact ? 300 : 366
                Layout.minimumWidth: containersPage.compact ? 280 : 300
                Layout.fillHeight: true
                selected: containerList.currentIndex >= 0
                          && containerList.currentIndex < containersPage.visibleItems.length
                          ? containersPage.visibleItems[containerList.currentIndex]
                          : null
            }
        }
    }

    AppDialog {
        id: createDialog
        title: qsTr("Novo container")
        modal: true
        width: 640
        anchors.centerIn: parent
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8
            GridLayout {
                columns: 2
                Layout.fillWidth: true
                columnSpacing: 10
                rowSpacing: 6

                Text { text: qsTr("Imagem"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: createImage
                    Layout.fillWidth: true
                    placeholderText: "nginx:alpine"
                }

                Text { text: qsTr("Nome"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: createName
                    Layout.fillWidth: true
                    placeholderText: qsTr("opcional")
                }

                Text { text: qsTr("Comando"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: createCommand
                    Layout.fillWidth: true
                    placeholderText: qsTr("opcional")
                }

                Text { text: qsTr("Portas"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: createPorts
                    Layout.fillWidth: true
                    placeholderText: "8080:80, 127.0.0.1:8081:81"
                }

                Text { text: qsTr("Env"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: createEnv
                    Layout.fillWidth: true
                    placeholderText: "A=B, C=D"
                }

                Text { text: qsTr("Volumes"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: createMounts
                    Layout.fillWidth: true
                    placeholderText: "/host:/container, /tmp:/mnt"
                }

                Text { text: qsTr("Workdir"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: createWorkDir
                    Layout.fillWidth: true
                    placeholderText: qsTr("opcional")
                }

                Text { text: qsTr("Usuário"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontMeta }
                FormField {
                    id: createUser
                    Layout.fillWidth: true
                    placeholderText: qsTr("opcional (uid:gid)")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                FormCheck { id: createRemove; text: qsTr("Remover ao parar") }
                FormCheck { id: createInit; text: qsTr("Init") }
                FormCheck { id: createPrivileged; text: qsTr("Privileged") }
                Item { Layout.fillWidth: true }
            }
        }
        footer: AppFooter {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                onClicked: createDialog.close()
            }
            ActionButton {
                text: qsTr("Criar e iniciar")
                variant: "primary"
                onClicked: {
                    backend.createContainer({
                        image: createImage.text,
                        name: createName.text,
                        command: createCommand.text,
                        portsText: createPorts.text,
                        envText: createEnv.text,
                        mountsText: createMounts.text,
                        workDir: createWorkDir.text,
                        user: createUser.text,
                        remove: createRemove.checked,
                        init: createInit.checked,
                        privileged: createPrivileged.checked
                    }, true)
                    createDialog.close()
                }
            }
        }
    }
}
