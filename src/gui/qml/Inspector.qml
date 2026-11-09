import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PodStack

Rectangle {
    id: inspector

    property var selected: null

    signal expandRequested(string id)

    color: Theme.bgSurface
    radius: Theme.radiusLg
    border.width: 1
    border.color: Theme.border
    clip: true

    readonly property string portsText: {
        if (!selected || !selected.ports) {
            return ""
        }
        if (Array.isArray(selected.ports)) {
            return selected.ports.join(", ")
        }
        return String(selected.ports)
    }

    readonly property string selectedId: selected ? (selected.id || "") : ""
    property var lanOverrides: ({})
    property bool lanPending: false
    property string lanPendingId: ""
    property bool lanTarget: false

    readonly property bool lanExposed: {
        if (!selected || portsText.length === 0) {
            return false
        }
        if (lanOverrides[selectedId] !== undefined) {
            return lanOverrides[selectedId]
        }
        return portsText.indexOf("127.0.0.1") < 0 && portsText.indexOf("::1") < 0
    }

    Connections {
        target: backend
        function onActionFinished(ok) {
            if (!inspector.lanPending) {
                return
            }
            inspector.lanPending = false
            if (ok && inspector.lanPendingId !== "") {
                var next = {}
                for (var key in inspector.lanOverrides) {
                    next[key] = inspector.lanOverrides[key]
                }
                next[inspector.lanPendingId] = inspector.lanTarget
                inspector.lanOverrides = next
            }
            inspector.lanPendingId = ""
        }
    }

    readonly property string health: selected ? (selected.health || "") : ""
    readonly property bool running: selected !== null && selected.state === "running"
    readonly property bool compact: width < 430

    property int tabIndex: 0

    function openTab(name) {
        if (name === "logs") {
            tabIndex = 1
        } else if (name === "stats") {
            tabIndex = 2
        } else if (name === "files") {
            tabIndex = 3
        } else {
            tabIndex = 0
        }
    }
    property string activeContainerId: ""

    onTabIndexChanged: {
        activeContainerId = selected ? (selected.id || "") : ""
        syncStreams()
    }

    Timer {
        id: selectionSync
        interval: 100
        onTriggered: {
            const id = inspector.selected ? (inspector.selected.id || "") : ""
            if (id !== inspector.activeContainerId) {
                inspector.activeContainerId = id
                inspector.syncStreams()
            }
        }
    }

    function syncStreams() {
        if (!selected) {
            backend.unsubscribeLogs()
            backend.unsubscribeStats()
            return
        }
        if (inspector.tabIndex === 1) {
            backend.unsubscribeStats()
            backend.subscribeLogs(selected.id, 200, true)
        } else if (inspector.tabIndex === 2) {
            backend.unsubscribeLogs()
            backend.subscribeStats(selected.id)
        } else if (inspector.tabIndex === 3) {
            backend.unsubscribeLogs()
            backend.unsubscribeStats()
            backend.listFiles(selected.id, "/")
        } else {
            backend.unsubscribeLogs()
            backend.unsubscribeStats()
        }
    }

    onSelectedChanged: selectionSync.restart()
    Component.onCompleted: {
        activeContainerId = selected ? (selected.id || "") : ""
        syncStreams()
    }
    Component.onDestruction: {
        backend.unsubscribeLogs()
        backend.unsubscribeStats()
    }

    // Estado vazio
    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width - 40
        visible: inspector.selected === null
        spacing: 6

        AppIcon {
            Layout.alignment: Qt.AlignHCenter
            name: "containers"
            size: 24
            color: Theme.textTertiary
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Nenhum container selecionado")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
        }
    }

    ColumnLayout {
        anchors.fill: parent
        visible: inspector.selected !== null
        spacing: 0

        // Cabeçalho
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 13
            Layout.bottomMargin: 11
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: inspector.selected ? inspector.selected.name : ""
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
                    font.letterSpacing: -0.3
                    elide: Text.ElideRight
                }

                AppBadge {
                    text: inspector.selected ? inspector.selected.state : ""
                    tone: inspector.running ? "success"
                                            : inspector.selected && inspector.selected.state === "paused"
                                              ? "warning" : "muted"
                }
            }

            Text {
                Layout.fillWidth: true
                text: inspector.selected ? inspector.selected.image : ""
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
                elide: Text.ElideRight
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 5

                ActionButton {
                    visible: inspector.running
                    text: inspector.compact ? "" : qsTr("Parar")
                    iconName: "stop"
                    size: "sm"
                    onClicked: backend.lifecycle("stop", inspector.selected.id)
                    Accessible.name: qsTr("Parar")
                }

                ActionButton {
                    visible: !inspector.running
                    text: inspector.compact ? "" : qsTr("Iniciar")
                    iconName: "play"
                    size: "sm"
                    variant: "primary"
                    onClicked: backend.lifecycle("start", inspector.selected.id)
                    Accessible.name: qsTr("Iniciar")
                }

                ActionButton {
                    text: inspector.compact ? "" : qsTr("Reiniciar")
                    iconName: "rotate"
                    size: "sm"
                    onClicked: backend.lifecycle("restart", inspector.selected.id)
                    Accessible.name: qsTr("Reiniciar")
                }

                ActionButton {
                    text: inspector.compact ? "" : qsTr("Terminal")
                    iconName: "terminal"
                    size: "sm"
                    onClicked: backend.openTerminal(inspector.selected.id)
                    Accessible.name: qsTr("Terminal")
                }

                ActionButton {
                    text: inspector.compact ? "" : qsTr("Expandir")
                    iconName: "maximize"
                    size: "sm"
                    onClicked: inspector.expandRequested(inspector.selected.id)
                    Accessible.name: qsTr("Expandir detalhe do container")
                }

                Item { Layout.fillWidth: true }

                ActionButton {
                    variant: "danger"
                    iconName: "trash"
                    size: "sm"
                    onClicked: {
                        removeDialog.containerId = inspector.selected.id
                        removeDialog.open()
                    }
                    Accessible.name: qsTr("Remover")
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        // Abas segmentadas
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 10
            radius: Theme.radiusSm
            color: Theme.bgSurface2
            border.width: 1
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.margins: 3
                spacing: 2

                Repeater {
                    model: [qsTr("Info"), qsTr("Logs"), qsTr("Stats"), qsTr("Files")]

                    ItemDelegate {
                        id: inspectorTab
                        readonly property bool active: inspector.tabIndex === index

                        Layout.fillWidth: true
                        implicitHeight: 24
                        hoverEnabled: true
                        Accessible.name: modelData

                        background: Rectangle {
                            radius: Theme.radiusXs
                            color: inspectorTab.active ? Theme.bgElevated
                                                       : inspectorTab.hovered ? Theme.bgHover
                                                                              : "transparent"
                        }

                        contentItem: Text {
                            text: modelData
                            color: inspectorTab.active ? Theme.textPrimary : Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMeta
                            font.weight: Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        onClicked: inspector.tabIndex = index
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 10
            currentIndex: inspector.tabIndex

            // Info
            ScrollView {
                id: infoScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true

                ColumnLayout {
                    width: infoScroll.availableWidth
                    spacing: 0

                    GridLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 14
                        Layout.rightMargin: 14
                        columns: infoScroll.availableWidth >= 460 ? 2 : 1
                        columnSpacing: 12
                        rowSpacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.maximumWidth: Math.max(0, infoScroll.availableWidth - 28)
                            spacing: 2

                            Text {
                                text: qsTr("ID")
                                color: Theme.textTertiary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMicro
                                font.weight: Font.DemiBold
                                font.capitalization: Font.AllUppercase
                                font.letterSpacing: 0.8
                            }

                            Text {
                                Layout.fillWidth: true
                                text: inspector.selected ? inspector.selected.id : "—"
                                color: Theme.textPrimary
                                font.family: Theme.monoFamily
                                font.pixelSize: Theme.fontMeta
                                wrapMode: Text.WrapAnywhere
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.maximumWidth: Math.max(0, infoScroll.availableWidth - 28)
                            spacing: 2

                            Text {
                                text: qsTr("PORTAS")
                                color: Theme.textTertiary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMicro
                                font.weight: Font.DemiBold
                                font.capitalization: Font.AllUppercase
                                font.letterSpacing: 0.8
                            }

                            Text {
                                Layout.fillWidth: true
                                text: inspector.portsText.length > 0 ? inspector.portsText : "—"
                                color: Theme.textPrimary
                                font.family: Theme.monoFamily
                                font.pixelSize: Theme.fontMeta
                                wrapMode: Text.WrapAnywhere
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                Layout.topMargin: 4
                                visible: inspector.portsText.length > 0
                                enabled: !inspector.lanPending
                                text: inspector.lanExposed ? qsTr("Fechar LAN")
                                                           : qsTr("Expor na LAN")
                                size: "sm"
                                onClicked: {
                                    inspector.lanPendingId = inspector.selected.id
                                    inspector.lanTarget = !inspector.lanExposed
                                    inspector.lanPending = true
                                    backend.exposeContainerLan(inspector.selected.id,
                                                               inspector.lanTarget)
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: inspector.portsText.length > 0
                                text: qsTr("Só portas publicadas. Containers antigos em 127.0.0.1 exigem recriação.")
                                color: Theme.textTertiary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMicro
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: inspector.portsText.length > 0
                                         && backend.lastError.length > 0
                                text: backend.lastError
                                color: Theme.danger
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMicro
                                wrapMode: Text.WordWrap
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.maximumWidth: Math.max(0, infoScroll.availableWidth - 28)
                            spacing: 2

                            Text {
                                text: qsTr("ESTADO")
                                color: Theme.textTertiary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMicro
                                font.weight: Font.DemiBold
                                font.capitalization: Font.AllUppercase
                                font.letterSpacing: 0.8
                            }

                            Text {
                                Layout.fillWidth: true
                                text: inspector.selected ? inspector.selected.state : "—"
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMeta
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.maximumWidth: Math.max(0, infoScroll.availableWidth - 28)
                            spacing: 2

                            Text {
                                text: qsTr("CRIADO")
                                color: Theme.textTertiary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMicro
                                font.weight: Font.DemiBold
                                font.capitalization: Font.AllUppercase
                                font.letterSpacing: 0.8
                            }

                            Text {
                                Layout.fillWidth: true
                                text: inspector.selected && inspector.selected.created
                                      ? inspector.selected.created : "—"
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMeta
                                elide: Text.ElideRight
                            }
                        }
                    }

                    Text {
                        Layout.leftMargin: 14
                        Layout.rightMargin: 14
                        Layout.topMargin: 14
                        Layout.bottomMargin: 6
                        text: qsTr("SAÚDE")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.8
                    }

                    RowLayout {
                        Layout.leftMargin: 14
                        Layout.rightMargin: 14
                        spacing: 5

                        AppBadge {
                            visible: inspector.health === "healthy"
                            text: qsTr("healthy")
                            tone: "success"
                        }

                        AppBadge {
                            visible: inspector.health === "unhealthy"
                            text: qsTr("unhealthy")
                            tone: "danger"
                        }

                        AppBadge {
                            visible: inspector.health === "starting"
                            text: qsTr("starting")
                            tone: "warning"
                        }

                        AppBadge {
                            visible: inspector.health.length === 0 || inspector.health === "none"
                            text: qsTr("sem healthcheck")
                            tone: "muted"
                        }
                    }

                    Text {
                        Layout.leftMargin: 14
                        Layout.rightMargin: 14
                        Layout.topMargin: 14
                        Layout.bottomMargin: 6
                        text: qsTr("AÇÕES")
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.8
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 14
                        Layout.rightMargin: 14
                        columns: infoScroll.availableWidth >= 460 ? 2 : 1
                        columnSpacing: 6
                        rowSpacing: 6

                        ActionButton {
                            Layout.fillWidth: true
                            text: qsTr("Pausar")
                            size: "sm"
                            onClicked: backend.lifecycle("pause", inspector.selected.id)
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: qsTr("Despausar")
                            size: "sm"
                            onClicked: backend.lifecycle("unpause", inspector.selected.id)
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: qsTr("Matar")
                            size: "sm"
                            variant: "danger"
                            onClicked: backend.lifecycle("kill", inspector.selected.id)
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: qsTr("Abrir porta")
                            size: "sm"
                            enabled: inspector.portsText.length > 0
                            onClicked: {
                                const match = inspector.portsText.match(/(\d+)->/)
                                if (match) {
                                    Qt.openUrlExternally("http://localhost:" + match[1])
                                }
                            }
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: qsTr("Renomear")
                            size: "sm"
                            onClicked: {
                                renameDialog.containerId = inspector.selected.id
                                renameName.text = inspector.selected.name
                                renameDialog.open()
                            }
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: qsTr("Duplicar")
                            size: "sm"
                            onClicked: backend.duplicateContainer(inspector.selected.id)
                        }
                    }

                    Text {
                        Layout.leftMargin: 14
                        Layout.rightMargin: 14
                        Layout.topMargin: 10
                        Layout.bottomMargin: 14
                        Layout.fillWidth: true
                        visible: backend.lastError.length > 0
                        text: backend.lastError
                        color: Theme.danger
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMicro
                        wrapMode: Text.WordWrap
                    }

                    Item { Layout.preferredHeight: 14 }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                LogsView { anchors.fill: parent; anchors.margins: 14 }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                StatsView { anchors.fill: parent; anchors.margins: 14 }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                FilesView {
                    anchors.fill: parent
                    anchors.margins: 14
                    containerId: inspector.selected ? inspector.selected.id : ""
                }
            }
        }
    }

    AppDialog {
        id: removeDialog
        property string containerId
        title: qsTr("Remover container?")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.border
        }
        contentItem: Text {
            text: qsTr("O container será removido permanentemente.")
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            wrapMode: Text.WordWrap
        }
        onAccepted: backend.removeContainer(containerId, true, false)
    }

    AppDialog {
        id: renameDialog
        property string containerId
        title: qsTr("Renomear container")
        modal: true
        width: 420
        anchors.centerIn: parent
        background: Rectangle {
            color: Theme.bgSurface
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 8

            Text {
                text: qsTr("Novo nome")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMeta
                font.weight: Font.DemiBold
            }

            FormField {
                id: renameName
                Layout.fillWidth: true
            }
        }
        footer: AppFooter {
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Cancelar")
                variant: "secondary"
                onClicked: renameDialog.close()
            }
            ActionButton {
                text: qsTr("Salvar")
                variant: "primary"
                onClicked: {
                    backend.renameContainer(renameDialog.containerId, renameName.text)
                    renameDialog.close()
                }
            }
        }
    }
}
